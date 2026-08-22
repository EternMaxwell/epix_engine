
#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <epix/render/view.hpp>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;
using namespace epix::render::view;
using namespace epix::render::camera;

namespace {
constexpr std::string_view kViewShaderWgsl = R"(
#define_import_path epix::view

struct EpixColorGrading {
    balance : mat3x3<f32>,
    saturation : vec3<f32>,
    contrast : vec3<f32>,
    gamma : vec3<f32>,
    gain : vec3<f32>,
    lift : vec3<f32>,
    midtone_range : vec2<f32>,
    exposure : f32,
    hue : f32,
    post_saturation : f32,
}

struct EpixView {
    clip_from_world : mat4x4<f32>,
    unjittered_clip_from_world : mat4x4<f32>,
    world_from_clip : mat4x4<f32>,
    world_from_view : mat4x4<f32>,
    view_from_world : mat4x4<f32>,
    clip_from_view : mat4x4<f32>,
    view_from_clip : mat4x4<f32>,
    world_position : vec3<f32>,
    exposure : f32,
    viewport : vec4<f32>,
    main_pass_viewport : vec4<f32>,
    frustum : array<vec4<f32>, 6>,
    color_grading : EpixColorGrading,
    mip_bias : f32,
    frame_count : u32,
}
)";

constexpr std::string_view kViewShaderSlang = R"(
module "epix/view";

namespace epix {
public struct ColorGrading {
    public float3x3 balance;
    public float3 saturation;
    public float3 contrast;
    public float3 gamma;
    public float3 gain;
    public float3 lift;
    public float2 midtone_range;
    public float exposure;
    public float hue;
    public float post_saturation;
};

public struct View {
    public float4x4 clip_from_world;
    public float4x4 unjittered_clip_from_world;
    public float4x4 world_from_clip;
    public float4x4 world_from_view;
    public float4x4 view_from_world;
    public float4x4 clip_from_view;
    public float4x4 view_from_clip;
    public float3 world_position;
    public float exposure;
    public float4 viewport;
    public float4 main_pass_viewport;
    public float4 frustum[6];
    public ColorGrading color_grading;
    public float mip_bias;
    public uint frame_count;
};
}
)";
}  // namespace

void view::prepare_view_target(Query<Item<Entity, const camera::ExtractedCamera&, const ExtractedView&, const Msaa&>> views,
                               Commands cmd,
                               Res<window::ExtractedWindows> extracted_windows,
                               Res<wgpu::Device> device,
                               ResMut<ViewTargetAttachments> view_target_attachments) {
    // Prepare the view target for each extracted camera view: the
    // double-buffered main textures (Bevy prepare_view_targets, view/mod.rs:1061)
    // and the final output attachment (the swapchain / image view). Nodes bind
    // `target.texture_view` which aliases the CURRENT main texture; the core
    // pipeline post-process nodes blit it to the output (Bevy core_pipeline).
    for (auto&& [entity, camera, view, msaa] : views.iter()) {
        std::optional<wgpu::TextureView> target_texture = std::visit(
            utils::visitor{
                [&](const wgpu::Texture& tex) -> std::optional<wgpu::TextureView> { return tex.createView(); },
                [&](const camera::WindowRef& win_ref) -> std::optional<wgpu::TextureView> {
                    auto&& id = win_ref.window_entity;
                    if (auto it = extracted_windows->windows.find(id);
                        it != extracted_windows->windows.end() && it->second.swapchain_texture_view) {
                        return it->second.swapchain_texture_view;
                    } else {
                        return std::nullopt;
                    }
                }},
            camera.render_target);
        std::optional<wgpu::TextureFormat> target_format = std::visit(
            utils::visitor{
                [&](const wgpu::Texture& tex) -> std::optional<wgpu::TextureFormat> { return tex.getFormat(); },
                [&](const camera::WindowRef& win_ref) -> std::optional<wgpu::TextureFormat> {
                    auto&& id = win_ref.window_entity;
                    if (auto it = extracted_windows->windows.find(id); it != extracted_windows->windows.end()) {
                        // Bevy uses swap_chain_texture_view_format for the out
                        // attachment (the sRGB-suffixed view format).
                        return it->second.swapchain_texture_view_format;
                    }
                    return std::nullopt;
                }},
            camera.render_target);
        if (!target_texture.has_value() || !target_texture.value()) {
            // invalid target texture, handle error;
            // no need to remove the entity, it will just be missing ViewTarget.
            continue;
        }
        if (!target_format.has_value()) {
            continue;
        }
        // Bevy prepare_view_targets sizes the main textures from the camera's
        // FULL target size (view/mod.rs:1218-1222); the camera viewport only
        // clips the render, it does not resize the target.
        const glm::uvec2 size = camera.target_size;
        // Bevy prepare_view_targets: the main texture format is Rgba16Float
        // when hdr, else TextureFormat::bevy_default() = Rgba8Unorm - NOT the
        // output format (the post-process blit converts in the fragment
        // shader, view/mod.rs:1061-1093).
        const wgpu::TextureFormat main_format =
            camera.hdr ? wgpu::TextureFormat::eRGBA16Float : wgpu::TextureFormat::eRGBA8Unorm;
        // View formats: the sRGB-suffixed twin when the main format is
        // non-sRGB (Bevy descriptor.view_formats).
        std::array<wgpu::TextureFormat, 1> view_formats{main_format};
        std::size_t view_format_count = 0;
        if (main_format == wgpu::TextureFormat::eRGBA8Unorm) {
            view_formats[0]    = wgpu::TextureFormat::eRGBA8UnormSrgb;
            view_format_count  = 1;
        } else if (main_format == wgpu::TextureFormat::eBGRA8Unorm) {
            view_formats[0]    = wgpu::TextureFormat::eBGRA8UnormSrgb;
            view_format_count  = 1;
        }
        const wgpu::TextureUsage main_usage =
            wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding;
        const std::uint32_t sample_count = view::samples(msaa);
        // MSAA sample texture shared by a and b (Bevy prepare_view_targets
        // creates main_texture_sampled when msaa.samples() > 1).
        std::optional<render_resource::CachedTexture> sampled_texture;
        if (sample_count > 1) {
            wgpu::Texture sampled = device->createTexture(
                wgpu::TextureDescriptor()
                    .setSize({std::max<std::uint32_t>(1, size.x), std::max<std::uint32_t>(1, size.y), 1})
                    .setFormat(main_format)
                    .setUsage(wgpu::TextureUsage::eRenderAttachment)
                    .setDimension(wgpu::TextureDimension::e2D)
                    .setSampleCount(sample_count)
                    .setMipLevelCount(1)
                    .setViewFormats(std::span<const wgpu::TextureFormat>(view_formats.data(), view_format_count))
                    .setLabel("main_texture_sampled"));
            sampled_texture = render_resource::CachedTexture{sampled, sampled.createView()};
        }
        auto make_main_attachment = [&](const char* label) {
            wgpu::Texture texture = device->createTexture(
                wgpu::TextureDescriptor()
                    .setSize({std::max<std::uint32_t>(1, size.x), std::max<std::uint32_t>(1, size.y), 1})
                    .setFormat(main_format)
                    .setUsage(main_usage)
                    .setDimension(wgpu::TextureDimension::e2D)
                    .setSampleCount(1)
                    .setMipLevelCount(1)
                    .setViewFormats(std::span<const wgpu::TextureFormat>(view_formats.data(), view_format_count))
                    .setLabel(label));
            render_resource::ColorAttachment attachment(render_resource::CachedTexture{texture, texture.createView()},
                                                        sampled_texture);
            if (camera.clear_color) {
                attachment.clear_color = glm::vec4(camera.clear_color->r, camera.clear_color->g,
                                                   camera.clear_color->b, camera.clear_color->a);
            }
            return attachment;
        };
        view::MainTargetTextures main_textures;
        main_textures.a = make_main_attachment("main_texture_a");
        main_textures.b = make_main_attachment("main_texture_b");

        view::ViewTarget target;
        target.main_textures       = std::move(main_textures);
        target.main_texture        = target.main_textures.main_texture;
        target.main_texture_format = main_format;
        // Bevy prepare_view_attachments (view/mod.rs:1138-1170): one shared
        // output attachment per render target, so the output is cleared at
        // most once per frame and later cameras composite over it instead of
        // wiping earlier cameras.
        const auto target_id = camera.render_target.identity();
        auto& attachments    = view_target_attachments->attachments;
        auto attachment_it   = attachments.find(target_id);
        if (attachment_it == attachments.end()) {
            attachment_it = attachments
                                .emplace(target_id,
                                         view::OutputColorAttachment::create(target_texture.value(), *target_format))
                                .first;
        }
        target.out_texture = attachment_it->second;
        target.texture_view        = target.main_textures.a.texture.default_view;
        target.format              = main_format;
        cmd.entity(entity).insert(std::move(target));
    }
}

void view::clear_view_attachments(ResMut<ViewTargetAttachments> view_target_attachments) {
    // Bevy clear_view_attachments (view/mod.rs:1042-1044): the per-frame
    // output attachments are dropped before window surfaces are reconfigured.
    view_target_attachments->attachments.clear();
}

void view::cleanup_view_targets_for_resize(Commands cmd,
                                           Res<window::ExtractedWindows> windows,
                                           Query<Item<Entity, const camera::ExtractedCamera&>> cameras) {
    // Bevy cleanup_view_targets_for_resize (view/mod.rs:1046-1059): when the
    // targeted window was resized (or its present mode changed), drop the
    // camera's ViewTarget so prepare_view_target recreates the main textures
    // at the new size.
    for (auto&& [entity, camera] : cameras.iter()) {
        if (auto* win_ref = std::get_if<camera::WindowRef>(&camera.render_target)) {
            epix::ecs::Entity window_entity = win_ref->primary ? windows->primary.value_or(epix::ecs::Entity{})
                                                                  : win_ref->window_entity;
            if (auto it = windows->windows.find(window_entity); it != windows->windows.end()) {
                if (it->second.size_changed || it->second.present_mode_changed) {
                    cmd.entity(entity).template remove<view::ViewTarget>();
                }
            }
        }
    }
}

void view::create_view_depth(Query<Item<Entity, const camera::ExtractedCamera&>> views,
                             Res<wgpu::Device> device,
                             Res<wgpu::Queue> queue,
                             ResMut<ViewDepthCache> depth_cache,
                             Commands cmd) {
    wgpu::CommandEncoder encoder = device->createCommandEncoder();
    for (auto&& [entity, camera] : views.iter()) {
        // Size the depth texture from the camera's full target size (Bevy
        // core_2d prepare_core_2d_depth_textures); the viewport only clips.
        glm::uvec2 size = camera.target_size;
        if (size.x == 0 || size.y == 0) {
            continue;  // invalid size
        }
        // create new depth texture
        wgpu::Texture texture;
        if (auto it = depth_cache->cache.find(size); it != depth_cache->cache.end()) {
            texture = std::move(it->second);
            depth_cache->cache.erase(it);
        } else {
            wgpu::TextureDescriptor desc;
            desc.setSize({size.x, size.y, 1})
                .setFormat(wgpu::TextureFormat::eDepth32Float)
                .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc)
                .setDimension(wgpu::TextureDimension::e2D)
                .setSampleCount(1)
                .setMipLevelCount(1)
                .setLabel("ViewDepthTexture");
            texture = device.get().createTexture(desc);
            if (!texture) {
                spdlog::error("Failed to create depth texture for view with size {}x{}", size.x, size.y);
                continue;
            }
        }
        wgpu::RenderPassDepthStencilAttachment depth_attachment;
        auto view = texture.createView();
        depth_attachment.setView(view)
            .setDepthLoadOp(wgpu::LoadOp::eClear)
            .setDepthStoreOp(wgpu::StoreOp::eStore)
            .setDepthClearValue(1.0f);
        wgpu::RenderPassEncoder pass =
            encoder.beginRenderPass(wgpu::RenderPassDescriptor().setDepthStencilAttachment(depth_attachment));
        pass.end();
        cmd.entity(entity).insert(view::ViewDepthTexture::create(std::move(texture), std::move(view)));
    }
    queue->submit(encoder.finish());
}

void clear_cache(ResMut<ViewDepthCache> depth_cache) { depth_cache->cache.clear(); }

void recycle_depth(Query<const view::ViewDepthTexture&> depths, ResMut<ViewDepthCache> depth_cache) {
    for (auto&& depth : depths.iter()) {
        if (depth.texture) {
            glm::uvec2 size{depth.texture.getWidth(), depth.texture.getHeight()};
            depth_cache->cache[size] = depth.texture;
        }
    }
}

std::size_t getOffsetInUniform(std::size_t index, std::size_t size, std::size_t alignment) {
    std::size_t stride = (size + alignment - 1) / alignment * alignment;
    return index * stride;
}

void create_uniform_for_view(
    Commands cmd,
    Query<Item<Entity,
               const view::ExtractedView&,
               const camera::ExtractedCamera&,
               Opt<const camera::MipBias&>,
               Opt<const view::Frustum&>>> views,
    Res<wgpu::Device> device,
    Res<wgpu::Limits> limits,
    ResMut<view::ViewUniforms> view_uniforms,
    Res<ViewUniformBindingLayout> uniform_layout,
    Res<wgpu::Queue> queue,
    Res<FrameCount> frame_count) {
    std::size_t uniform_size = sizeof(ViewUniform);
    std::size_t alignment    = limits->minUniformBufferOffsetAlignment;
    // Populate the ViewUniforms dynamic uniform buffer (Bevy prepare_view_uniforms,
    // view/mod.rs:906-969).
    view_uniforms->uniforms.dynamic_offset_alignment = alignment;
    view_uniforms->uniforms.values.clear();
    view_uniforms->offsets.clear();
    // Bevy get_writer returns None when there are no views: nothing to upload
    // (uniform_buffer.rs:270) 鈥?skip silently instead of logging an error.
    if (views.iter().max_remaining() == 0) {
        return;
    }
    for (auto&& [entity, view, camera, opt_mip_bias, opt_frustum] : views.iter()) {
        const glm::mat4 clip_from_view  = view.projection;
        const glm::mat4 view_from_clip  = glm::inverse(clip_from_view);
        const glm::mat4 world_from_view = view.transform.matrix;
        const glm::mat4 view_from_world = glm::inverse(world_from_view);
        // No temporal jitter in epix yet: clip_from_world is either the cached
        // value or clip_from_view * view_from_world (Bevy same fallback).
        const glm::mat4 clip_from_world = view.clip_from_world.value_or(clip_from_view * view_from_world);
        const glm::vec4 viewport_vec    = glm::vec4(view.viewport);
        ViewUniform uniform{
            .clip_from_world           = clip_from_world,
            .unjittered_clip_from_world = clip_from_view * view_from_world,
            .world_from_clip           = world_from_view * view_from_clip,
            .world_from_view           = world_from_view,
            .view_from_world           = view_from_world,
            .clip_from_view            = clip_from_view,
            .view_from_clip            = view_from_clip,
            .world_position            = glm::vec3(world_from_view[3]),
            .exposure                  = 1.0f,  // Bevy Exposure::default(); no Exposure component yet
            .viewport                  = viewport_vec,
            .main_pass_viewport        = viewport_vec,  // no MainPassResolutionOverride
            .frustum                   = opt_frustum ? opt_frustum->get().planes : std::array<glm::vec4, 6>{},
            .color_grading             = {},             // default identity; no per-camera ColorGrading yet
            .mip_bias                  = opt_mip_bias ? opt_mip_bias->get().bias : 0.0f,
            .frame_count               = frame_count.get().count,
        };
        std::size_t offset = view_uniforms->uniforms.push(uniform);
        view_uniforms->offsets.push_back(static_cast<std::uint32_t>(offset));
        cmd.entity(entity).insert(ViewUniformOffset{static_cast<std::uint32_t>(offset)});
    }
    // Create (if needed) and upload the GPU buffer, then build per-view bind
    // groups with dynamic offsets into it.
    view_uniforms->uniforms.write_buffer(device.get(), queue.get());
    wgpu::Buffer buffer = view_uniforms->uniforms.buffer;
    if (!buffer) {
        spdlog::error("Failed to create uniform buffer for views");
        return;
    }
    std::size_t index = 0;
    for (auto&& [entity, view, camera, opt_mip_bias, opt_frustum] : views.iter()) {
        (void)opt_mip_bias;
        (void)opt_frustum;
        std::uint32_t offset = view_uniforms->offsets[index];
        wgpu::BindGroup bind_group =
            device.get().createBindGroup(wgpu::BindGroupDescriptor()
                                             .setLayout(uniform_layout->layout)
                                             .setEntries(std::array{
                                                 wgpu::BindGroupEntry()
                                                     .setBinding(0)
                                                     .setBuffer(buffer)
                                                     .setOffset(offset)
                                                     .setSize(sizeof(ViewUniform)),
                                             })
                                             .setLabel("ViewUniformBindGroup"));
        cmd.entity(entity).insert(ViewBindGroup{std::move(bind_group)});
        index++;
    }
}

void view::ViewPlugin::attach(App& app) {
    spdlog::debug("[render.view] Attaching ViewPlugin.");

    // Register view shader libraries into the embedded asset registry
    {
        auto& world   = app.world_mut();
        auto registry = world.get_resource_mut<epix::assets::EmbeddedAssetRegistry>();
        auto server   = world.get_resource<epix::assets::AssetServer>();
        if (registry && server) {
            // WGSL
            {
                auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(kViewShaderWgsl.data()),
                                                        kViewShaderWgsl.size());
                registry->get().insert_asset_static("epix/shaders/view.wgsl", bytes);
                auto handle = server->get().load<epix::shader::Shader>("embedded://epix/shaders/view.wgsl");
                static auto permanent_wgsl = std::move(handle);
            }
            // Slang
            {
                auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(kViewShaderSlang.data()),
                                                        kViewShaderSlang.size());
                registry->get().insert_asset_static("epix/shaders/view.slang", bytes);
                auto handle = server->get().load<epix::shader::Shader>("embedded://epix/shaders/view.slang");
                static auto permanent_slang = std::move(handle);
            }
        } else {
            spdlog::warn(
                "[render.view] EmbeddedAssetRegistry or AssetServer not available. View shader library not "
                "registered.");
        }
    }
    // Bevy ViewPlugin adds RenderVisibilityRangePlugin (view/mod.rs:105-110).
    RenderVisibilityRangePlugin{}.attach(app);
    ViewUniformBindingLayout view_uniform_binding_layout(app.world_mut());
    app.world_mut().insert_resource(view_uniform_binding_layout);
    if (auto sub_app = app.get_sub_app_mut(render::Render)) {
        sub_app->get().world_mut().insert_resource(view_uniform_binding_layout);
        sub_app->get().world_mut().insert_resource(ViewDepthCache{});
        // Bevy view/mod.rs:141-142: ViewUniforms + ViewTargetAttachments are
        // initialized in the render app (clear_view_attachments needs the latter).
        sub_app->get().world_mut().init_resource<ViewTargetAttachments>();
        // Bevy ViewUniforms::from_world (view/mod.rs:598-609): label +
        // conditional STORAGE usage when storage buffers are available.
        {
            view::ViewUniforms view_uniforms;
            view_uniforms.uniforms.label = "view_uniforms_buffer";
            if (auto limits = sub_app->get().world().get_resource<wgpu::Limits>();
                limits && limits->get().maxStorageBuffersPerShaderStage > 0) {
                view_uniforms.uniforms.usage =
                    view_uniforms.uniforms.usage | wgpu::BufferUsage::eStorage;
            }
            sub_app->get().world_mut().insert_resource(std::move(view_uniforms));
        }
        // Bevy ViewPlugin: clear_view_attachments + cleanup_view_targets_for_resize
        // run in ManageViews before create_surfaces (view/mod.rs:116-122) so
        // stale TextureViews are dropped before surface reconfiguration.
        sub_app->get().add_systems(
            Render,
            into(clear_view_attachments, cleanup_view_targets_for_resize)
                .before(window::create_surfaces)
                .in_set(RenderSystems::ManageViews)
                .set_names(std::array{"clear view attachments", "cleanup view targets for resize"}));
        sub_app->get().add_systems(Render, into(prepare_view_target, create_view_depth)
                                               .after(window::prepare_windows)
                                               .in_set(RenderSystems::ManageViews)
                                               .set_names(std::array{"prepare view targets", "create view depths"}));
        sub_app->get().add_systems(Render,
                                   into(clear_cache).after(RenderSystems::ManageViews).set_name("clear view depth cache"));
        sub_app->get().add_systems(
            Render,
            into(create_uniform_for_view).in_set(render::RenderSystems::PrepareResources).set_name("create view uniforms"));
        sub_app->get().add_systems(
            Render,
            into(recycle_depth).after(RenderSystems::Render).before(RenderSystems::Cleanup).set_name("recycle view depths"));
    }
}



void camera::extract_cameras(
    Commands cmd,
    Res<ClearColor> global_clear_color,
    Extract<Query<Item<Entity,
                       const Camera&,
                       const CameraRenderGraph&,
                       const transform::GlobalTransform&,
                       const view::VisibleEntities&,
                       const view::Frustum&,
                       Opt<const RenderLayers&>,
                       Opt<const camera::MipBias&>,
                       const view::Msaa&>>> cameras,
    Extract<Query<Entity, With<::epix::window::PrimaryWindow, ::epix::window::Window>>> primary_window) {
    // extract camera entities to render world, this will spawn an related
    // entity with ExtractedCamera, ExtractedView and other components.

    auto primary = primary_window.single();

    for (auto&& [entity, camera, graph, gtransform, visible_entities, frustum, opt_render_layer, opt_mip_bias, msaa] :
         cameras.iter()) {
        if (!camera.active) continue;
        auto target_size = camera.get_target_size();
        if (target_size.x == 0 || target_size.y == 0) continue;
        auto normalized_target = camera.render_target.normalize(primary);
        if (!normalized_target.has_value()) continue;
        auto viewport_size   = camera.get_viewport_size();
        auto viewport_origin = camera.get_viewport_origin();

        auto commands = cmd.spawn(epix::render::sync_world::TemporaryRenderEntity{});
        // single call to insert to reduce overhead
        commands.insert(
            ExtractedCamera{
                .render_target = *normalized_target,
                .viewport_size = viewport_size,
                .target_size   = target_size,
                .viewport      = camera.viewport,
                .render_graph  = graph,
                .order         = camera.order,
                .clear_color   = [&]() -> std::optional<ClearColor> {
                    if (camera.clear_color.type == ClearColorConfig::Type::Global ||
                        camera.clear_color.type == ClearColorConfig::Type::Default) {
                        return *global_clear_color;
                    } else if (camera.clear_color.type == ClearColorConfig::Type::Custom) {
                        return ClearColor(camera.clear_color.clear_color);
                    } else {
                        return std::nullopt;
                    }
                }(),
                .hdr          = camera.hdr,
                .render_layer = opt_render_layer ? *opt_render_layer : RenderLayers::layer(0),
            },
            view::ExtractedView{
                .retained_view_entity =
                    view::RetainedViewEntity{sync_world::MainEntity{entity}, std::nullopt, 0},
                .projection = camera.computed.projection,
                .transform  = gtransform,
                .hdr        = camera.hdr,
                .viewport   = glm::uvec4(viewport_origin.x, viewport_origin.y, viewport_size.x, viewport_size.y),
            },
            visible_entities,
            // Bevy extracts Msaa via ExtractComponentPlugin (view/mod.rs:107).
            view::Msaa{msaa},
            view::Frustum{frustum});
        // Bevy extracts MipBias only when the camera has one (ExtractComponentPlugin);
        // prepare_view_uniforms then falls back to 0.0 when absent.
        if (opt_mip_bias) {
            commands.insert(camera::MipBias{opt_mip_bias->get().bias});
        }
    }
}

void epix::render::camera::CameraDriverNode::run(graph::GraphContext& graph, graph::RenderContext& render_ctx,
                                                   const World& world) {
    // Iterate cameras in sorted order (Bevy CameraDriverNode uses SortedCameras).
    auto sorted = world.get_resource<SortedCameras>();
    if (!sorted) return;
    auto cameras = world.try_query<Item<Entity, const ExtractedCamera&, const view::ViewTarget&>>();
    if (!cameras) return;
    auto encoder = render_ctx.command_encoder();
    for (const auto& sorted_camera : sorted->get().cameras) {
        auto opt = cameras->query(world).get(sorted_camera.entity);
        if (!opt) continue;
        auto&& [entity, camera, target] = *opt;
        if (camera.clear_color) {
            auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(std::array{
                wgpu::RenderPassColorAttachment()
                    .setView(target.texture_view)
                    .setLoadOp(wgpu::LoadOp::eClear)
                    .setStoreOp(wgpu::StoreOp::eStore)
                    .setDepthSlice(~0u)
                    .setClearValue(wgpu::Color(camera.clear_color->r, camera.clear_color->g, camera.clear_color->b,
                                               camera.clear_color->a)),
            }));
            render_pass.end();
        }
        // Bevy CameraDriverNode (view/camera_driver_node.rs): the camera's
        // render graph clears, renders the main passes and the post-process
        // blit to the output; the driver only runs the graph.
        if (!graph.run_sub_graph(camera.render_graph, {}, entity)) {
            spdlog::warn("Failed to run camera render graph for entity {:#x}, with render graph label {}", entity.index,
                         camera.render_graph.type_index().short_name());
        }
    }
}


