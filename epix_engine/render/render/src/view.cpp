
#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <epix/render/manual_texture_view.hpp>
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

void view::prepare_view_target(Query<Item<Entity,
                                          const camera::ExtractedCamera&,
                                          const ExtractedView&,
                                          const ::epix::camera::CameraMainTextureUsages&,
                                          const view::Msaa&>> views,
                               Commands cmd,
                               Res<::epix::camera::ClearColor> global_clear_color,
                               Res<wgpu::Device> device,
                               ResMut<render_resource::TextureCache> texture_cache,
                               Res<ViewTargetAttachments> view_target_attachments) {
    // NormalizedRenderTarget guarantees primary-window resolution occurred
    // during extraction. Prepare the view target for each extracted camera view: the
    // double-buffered main textures (Bevy prepare_view_targets, view/mod.rs:1061)
    // and the final output attachment (the swapchain / image view). Nodes bind
    // `target.texture_view` which aliases the CURRENT main texture; the core
    // pipeline post-process nodes blit it to the output (Bevy core_pipeline).
    struct SharedMainTextures {
        render_resource::CachedTexture a;
        render_resource::CachedTexture b;
        std::optional<render_resource::CachedTexture> sampled;
        std::shared_ptr<std::atomic<std::uint32_t>> main_texture;
    };
    // TextureCache handles reuse between frames. Bevy additionally shares
    // compatible A/B targets among cameras during one preparation pass.
    std::unordered_map<std::string, SharedMainTextures> shared_main_textures;
    for (auto&& [entity, camera, view, texture_usage, msaa] : views.iter()) {
        if (!camera.target || !camera.physical_target_size) continue;
        const auto& normalized_target = *camera.target;
        const auto attachment_it      = view_target_attachments->attachments.find(normalized_target.identity());
        if (attachment_it == view_target_attachments->attachments.end()) {
            // Match Bevy: an unavailable output attachment invalidates this
            // frame's ViewTarget rather than retaining a stale texture view.
            cmd.entity(entity).template remove<view::ViewTarget>();
            continue;
        }
        // Bevy prepare_view_targets sizes the main textures from the camera's
        // FULL target size (view/mod.rs:1218-1222); the camera viewport only
        // clips the render, it does not resize the target.
        const glm::uvec2 size = *camera.physical_target_size;
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
            view_formats[0]   = wgpu::TextureFormat::eRGBA8UnormSrgb;
            view_format_count = 1;
        } else if (main_format == wgpu::TextureFormat::eBGRA8Unorm) {
            view_formats[0]   = wgpu::TextureFormat::eBGRA8UnormSrgb;
            view_format_count = 1;
        }
        const wgpu::TextureUsage main_usage = texture_usage.usage;
        const std::uint32_t sample_count    = view::samples(msaa);
        const auto target_identity          = normalized_target.identity();
        const auto shared_key = std::to_string(target_identity.kind) + ':' + std::to_string(target_identity.value) +
                                ':' + std::to_string(static_cast<std::uint64_t>(main_usage)) + ':' +
                                std::to_string(camera.hdr) + ':' + std::to_string(sample_count) + ':' +
                                std::to_string(size.x) + ':' + std::to_string(size.y);
        auto [shared_it, inserted] = shared_main_textures.try_emplace(shared_key);
        if (inserted) {
            auto& shared = shared_it->second;
            // MSAA sample texture shared by A and B (Bevy
            // prepare_view_targets creates main_texture_sampled).
            if (sample_count > 1) {
                wgpu::TextureDescriptor descriptor;
                descriptor.setSize({std::max<std::uint32_t>(1, size.x), std::max<std::uint32_t>(1, size.y), 1})
                    .setFormat(main_format)
                    .setUsage(wgpu::TextureUsage::eRenderAttachment)
                    .setDimension(wgpu::TextureDimension::e2D)
                    .setSampleCount(sample_count)
                    .setMipLevelCount(1)
                    .setViewFormats(std::span<const wgpu::TextureFormat>(view_formats.data(), view_format_count))
                    .setLabel("main_texture_sampled");
                shared.sampled =
                    texture_cache->get(*device,
                                       render_resource::TextureCacheKey{
                                           .format       = main_format,
                                           .width        = std::max(1u, size.x),
                                           .height       = std::max(1u, size.y),
                                           .sample_count = sample_count,
                                           .usage        = wgpu::TextureUsage::eRenderAttachment,
                                           .label        = "main_texture_sampled",
                                           .view_formats = std::vector<wgpu::TextureFormat>(
                                               view_formats.begin(), view_formats.begin() + view_format_count)},
                                       descriptor);
            }
            auto make_main_texture = [&](const char* label) {
                wgpu::TextureDescriptor descriptor;
                descriptor.setSize({std::max<std::uint32_t>(1, size.x), std::max<std::uint32_t>(1, size.y), 1})
                    .setFormat(main_format)
                    .setUsage(main_usage)
                    .setDimension(wgpu::TextureDimension::e2D)
                    .setSampleCount(1)
                    .setMipLevelCount(1)
                    .setViewFormats(std::span<const wgpu::TextureFormat>(view_formats.data(), view_format_count))
                    .setLabel(label);
                return texture_cache->get(*device,
                                          render_resource::TextureCacheKey{
                                              .format       = main_format,
                                              .width        = std::max(1u, size.x),
                                              .height       = std::max(1u, size.y),
                                              .sample_count = 1,
                                              .usage        = main_usage,
                                              .label        = label,
                                              .view_formats = std::vector<wgpu::TextureFormat>(
                                                  view_formats.begin(), view_formats.begin() + view_format_count)},
                                          descriptor);
            };
            shared.a            = make_main_texture("main_texture_a");
            shared.b            = make_main_texture("main_texture_b");
            shared.main_texture = std::make_shared<std::atomic<std::uint32_t>>(0);
        }
        const auto clear_color = [&]() -> std::optional<glm::vec4> {
            if (std::holds_alternative<::epix::camera::ClearColorConfig::None>(camera.clear_color)) return std::nullopt;
            if (const auto* custom = std::get_if<::epix::camera::ClearColorConfig::Custom>(&camera.clear_color)) {
                return custom->color.to_vec4();
            }
            return global_clear_color->to_vec4();
        }();
        const auto& shared = shared_it->second;
        view::MainTargetTextures main_textures;
        main_textures.a = render_resource::ColorAttachment(shared.a, shared.sampled, std::nullopt, clear_color);
        main_textures.b = render_resource::ColorAttachment(shared.b, shared.sampled, std::nullopt, clear_color);
        main_textures.main_texture = shared.main_texture;

        view::ViewTarget target;
        target.main_textures        = std::move(main_textures);
        target.main_texture_format_ = main_format;
        target.output_attachment    = attachment_it->second;
        target.texture_view         = target.main_textures.a.texture.default_view;
        target.format               = main_format;
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
        if (camera.target)
            if (auto* win_ref = std::get_if<::epix::window::NormalizedWindowRef>(&*camera.target)) {
                if (auto it = windows->windows.find(win_ref->entity()); it != windows->windows.end()) {
                    if (it->second.size_changed || it->second.present_mode_changed) {
                        cmd.entity(entity).template remove<view::ViewTarget>();
                    }
                }
            }
    }
}

void view::create_view_depth(Query<Item<Entity, const camera::ExtractedCamera&, const view::Msaa&>> views,
                             Res<wgpu::Device> device,
                             ResMut<ViewDepthCache> depth_cache,
                             Commands cmd) {
    for (auto&& [entity, camera, msaa] : views.iter()) {
        // Size the depth texture from the camera's full target size (Bevy
        // core_2d prepare_core_2d_depth_textures); the viewport only clips.
        if (!camera.physical_target_size) continue;
        glm::uvec2 size = *camera.physical_target_size;
        if (size.x == 0 || size.y == 0) {
            continue;  // invalid size
        }
        const ViewDepthCacheKey cache_key{size, samples(msaa)};
        // Create a depth texture with the same sample count as the main pass.
        wgpu::Texture texture;
        if (auto it = depth_cache->cache.find(cache_key); it != depth_cache->cache.end()) {
            texture = std::move(it->second);
            depth_cache->cache.erase(it);
        } else {
            wgpu::TextureDescriptor desc;
            desc.setSize({size.x, size.y, 1})
                .setFormat(wgpu::TextureFormat::eDepth32Float)
                // Bevy's view depth textures are attachment-only. In
                // particular, COPY_SRC is invalid/unusable for a
                // multisampled texture and poisons the command encoder.
                .setUsage(wgpu::TextureUsage::eRenderAttachment)
                .setDimension(wgpu::TextureDimension::e2D)
                .setSampleCount(cache_key.sample_count)
                .setMipLevelCount(1)
                .setLabel("ViewDepthTexture");
            texture = device.get().createTexture(desc);
            if (!texture) {
                spdlog::error("Failed to create depth texture for view with size {}x{}", size.x, size.y);
                continue;
            }
        }
        auto view = texture.createView();
        // The first main pass clears this attachment (Bevy
        // ViewDepthTexture::get_attachment); no separate depth-only command
        // buffer is needed.
        cmd.entity(entity).insert(
            view::ViewDepthTexture{std::move(texture), render_resource::DepthAttachment(std::move(view), 0.0f)});
    }
}

void clear_cache(ResMut<ViewDepthCache> depth_cache) { depth_cache->cache.clear(); }

void recycle_depth(Query<const view::ViewDepthTexture&> depths, ResMut<ViewDepthCache> depth_cache) {
    for (auto&& depth : depths.iter()) {
        if (depth.texture) {
            const ViewDepthCacheKey key{{depth.texture.getWidth(), depth.texture.getHeight()},
                                        depth.texture.getSampleCount()};
            depth_cache->cache[key] = depth.texture;
        }
    }
}

std::size_t getOffsetInUniform(std::size_t index, std::size_t size, std::size_t alignment) {
    std::size_t stride = (size + alignment - 1) / alignment * alignment;
    return index * stride;
}

void create_uniform_for_view(Commands cmd,
                             Query<Item<Entity,
                                        const view::ExtractedView&,
                                        const camera::ExtractedCamera&,
                                        Opt<const camera::MipBias&>,
                                        Opt<const ::epix::camera::Frustum&>,
                                        Opt<const camera::TemporalJitter&>,
                                        Opt<const ::epix::camera::MainPassResolutionOverride&>>> views,
                             Res<wgpu::Device> device,
                             Res<wgpu::Limits> limits,
                             ResMut<view::ViewUniforms> view_uniforms,
                             Res<ViewUniformBindingLayout> uniform_layout,
                             Res<wgpu::Queue> queue,
                             Res<FrameCount> frame_count) {
    // Populate the ViewUniforms dynamic uniform buffer (Bevy prepare_view_uniforms,
    // view/mod.rs:906-969). Its device alignment is established when the
    // ViewUniforms resource is initialized in ViewPlugin::attach.
    view_uniforms->uniforms.clear();
    view_uniforms->offsets.clear();
    // Bevy get_writer returns None when there are no views: nothing to upload
    // (uniform_buffer.rs:270) 鈥?skip silently instead of logging an error.
    if (views.iter().max_remaining() == 0) {
        return;
    }
    for (auto&& [entity, view, camera, opt_mip_bias, opt_frustum, opt_temporal_jitter, opt_resolution_override] :
         views.iter()) {
        const glm::mat4 unjittered_projection = view.clip_from_view;
        glm::mat4 clip_from_view              = unjittered_projection;
        if (opt_temporal_jitter) {
            opt_temporal_jitter->get().jitter_projection(clip_from_view, glm::vec2(view.viewport.z, view.viewport.w));
        }
        const glm::mat4 view_from_clip         = glm::inverse(clip_from_view);
        const glm::mat4 world_from_view_matrix = view.world_from_view.matrix;
        const glm::mat4 view_from_world        = glm::inverse(world_from_view_matrix);
        const glm::mat4 clip_from_world        = opt_temporal_jitter
                                                     ? clip_from_view * view_from_world
                                                     : view.clip_from_world.value_or(clip_from_view * view_from_world);
        const glm::vec4 viewport_vec           = glm::vec4(view.viewport);
        glm::vec4 main_pass_viewport           = viewport_vec;
        if (opt_resolution_override) {
            main_pass_viewport.z = static_cast<float>(opt_resolution_override->get().size.x);
            main_pass_viewport.w = static_cast<float>(opt_resolution_override->get().size.y);
        }
        ViewUniform uniform{
            .clip_from_world            = clip_from_world,
            .unjittered_clip_from_world = unjittered_projection * view_from_world,
            .world_from_clip            = world_from_view_matrix * view_from_clip,
            .world_from_view            = world_from_view_matrix,
            .view_from_world            = view_from_world,
            .clip_from_view             = clip_from_view,
            .view_from_clip             = view_from_clip,
            .world_position             = glm::vec3(world_from_view_matrix[3]),
            .exposure                   = camera.exposure,
            .viewport                   = viewport_vec,
            .main_pass_viewport         = main_pass_viewport,
            .frustum                    = opt_frustum ? opt_frustum->get().planes : std::array<glm::vec4, 6>{},
            .color_grading              = view::to_uniform(view.color_grading),
            .mip_bias                   = opt_mip_bias ? opt_mip_bias->get().bias : 0.0f,
            .frame_count                = frame_count.get().count,
        };
        std::size_t offset = view_uniforms->uniforms.push(uniform);
        view_uniforms->offsets.push_back(static_cast<std::uint32_t>(offset));
        cmd.entity(entity).insert(ViewUniformOffset{static_cast<std::uint32_t>(offset)});
    }
    // Create (if needed) and upload the GPU buffer, then build per-view bind
    // groups with dynamic offsets into it.
    view_uniforms->uniforms.write_buffer(device.get(), queue.get());
    const auto* buffer = view_uniforms->uniforms.buffer();
    if (!buffer) {
        spdlog::error("Failed to create uniform buffer for views");
        return;
    }
    std::size_t index = 0;
    for (auto&& [entity, view, camera, opt_mip_bias, opt_frustum, opt_temporal_jitter, opt_resolution_override] :
         views.iter()) {
        (void)opt_mip_bias;
        (void)opt_frustum;
        (void)opt_temporal_jitter;
        (void)opt_resolution_override;
        std::uint32_t offset       = view_uniforms->offsets[index];
        wgpu::BindGroup bind_group = device.get().createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLayout(uniform_layout->layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(*buffer).setOffset(offset).setSize(
                        sizeof(ViewUniform)),
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
    if (auto sub_app = app.get_sub_app_mut(render::Render)) {
        // Bevy creates this render-device-dependent state only in the
        // RenderApp. Keeping it out of the main world also permits ViewPlugin
        // to be used without a renderer, as Bevy permits.
        auto& render_world = sub_app->get().world_mut();
        render_world.insert_resource(ViewUniformBindingLayout(render_world));
        render_world.insert_resource(ViewDepthCache{});
        // Bevy view/mod.rs:141-142: ViewUniforms + ViewTargetAttachments are
        // initialized in the render app (clear_view_attachments needs the latter).
        sub_app->get().world_mut().init_resource<ViewTargetAttachments>();
        // Bevy ViewUniforms::from_world (view/mod.rs:598-609): label +
        // conditional STORAGE usage when storage buffers are available.
        {
            view::ViewUniforms view_uniforms;
            if (auto limits = sub_app->get().world().get_resource<wgpu::Limits>()) {
                view_uniforms.uniforms = render_resource::DynamicUniformBuffer<view::ViewUniform>::new_with_alignment(
                    limits->get().minUniformBufferOffsetAlignment);
            }
            view_uniforms.uniforms.set_label("view_uniforms_buffer");
            if (auto limits = sub_app->get().world().get_resource<wgpu::Limits>();
                limits && limits->get().maxStorageBuffersPerShaderStage > 0) {
                view_uniforms.uniforms.add_usages(wgpu::BufferUsage::eStorage);
            }
            sub_app->get().world_mut().insert_resource(std::move(view_uniforms));
        }
        // Bevy ViewPlugin: clear_view_attachments + cleanup_view_targets_for_resize
        // run in ManageViews before create_surfaces (view/mod.rs:116-122) so
        // stale TextureViews are dropped before surface reconfiguration.
        sub_app->get().add_systems(
            Render, into(clear_view_attachments, cleanup_view_targets_for_resize)
                        .before(window::create_surfaces)
                        .in_set(RenderSystems::ManageViews)
                        .set_names(std::array{"clear view attachments", "cleanup view targets for resize"}));
        sub_app->get().add_systems(Render, into(prepare_view_attachments)
                                               .after(window::prepare_windows)
                                               .before(prepare_view_target)
                                               .in_set(RenderSystems::ManageViews)
                                               .set_name("prepare view attachments"));
        sub_app->get().add_systems(Render, into(prepare_view_target, create_view_depth)
                                               .after(prepare_view_attachments)
                                               .in_set(RenderSystems::ManageViews)
                                               .set_names(std::array{"prepare view targets", "create view depths"}));
        sub_app->get().add_systems(
            Render, into(clear_cache).after(RenderSystems::ManageViews).set_name("clear view depth cache"));
        sub_app->get().add_systems(Render, into(create_uniform_for_view)
                                               .in_set(render::RenderSystems::PrepareResources)
                                               .set_name("create view uniforms"));
        sub_app->get().add_systems(Render, into(recycle_depth)
                                               .after(RenderSystems::Render)
                                               .before(RenderSystems::Cleanup)
                                               .set_name("recycle view depths"));
    }
}

void epix::render::camera::CameraPlugin::attach(App& app) {
    // Bevy render::camera::CameraPlugin owns render-facing requirements.
    // Keep them distinct from epix::camera::CameraPlugin, which owns only
    // camera-module projection and visibility setup.
    app.world_mut().register_required_components_with<::epix::camera::Camera>([] { return view::Msaa::Sample4; });
    app.world_mut().register_required_components_with<::epix::camera::Camera>(
        [] { return sync_world::SyncToRenderWorld{}; });
    app.world_mut().register_required_components_with<::epix::camera::Camera3d>([] { return view::ColorGrading{}; });
    app.world_mut().register_required_components_with<::epix::camera::Camera3d>(
        [] { return ::epix::camera::Exposure{}; });

    app.sub_app_mut(Render).then([](App& render_app) {
        render_app.world_mut().init_resource<SortedCameras>();
        render_app.add_systems(ExtractSchedule, into(extract_cameras).set_name("extract cameras"));
        render_app.add_systems(Render, into(sort_cameras).in_set(RenderSystems::ManageViews).set_name("sort cameras"));
        if (auto render_graph = render_app.get_resource_mut<graph::RenderGraph>()) {
            render_graph->get().add_node(CameraDriverNodeLabel, CameraDriverNode{});
        }
    });

    // Bevy render::camera::CameraPlugin extracts ClearColor and owns the
    // main-world camera target update path, including manual texture views.
    app.add_plugins(ExtractResourcePlugin<::epix::camera::ClearColor>{});
    app.add_systems(app::PostStartup, into(view::update_manual_texture_view_cameras<::epix::camera::Projection>)
                                          .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                                          .set_name("startup update manual texture view cameras"));
    app.add_systems(app::PostStartup,
                    into(view::update_manual_texture_view_cameras<::epix::camera::OrthographicProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("startup update manual texture view orthographic cameras"));
    app.add_systems(app::PostStartup,
                    into(view::update_manual_texture_view_cameras<::epix::camera::PerspectiveProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("startup update manual texture view perspective cameras"));
    app.add_systems(app::PostUpdate, into(view::update_manual_texture_view_cameras<::epix::camera::Projection>)
                                         .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                                         .set_name("update manual texture view cameras"));
    app.add_systems(app::PostUpdate,
                    into(view::update_manual_texture_view_cameras<::epix::camera::OrthographicProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("update manual texture view orthographic cameras"));
    app.add_systems(app::PostUpdate,
                    into(view::update_manual_texture_view_cameras<::epix::camera::PerspectiveProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("update manual texture view perspective cameras"));
}

void camera::extract_cameras(
    Commands cmd,
    Extract<Query<Item<Entity,
                       const ::epix::camera::Camera&,
                       const ::epix::camera::RenderTarget&,
                       const CameraRenderGraph&,
                       const transform::GlobalTransform&,
                       const ::epix::camera::VisibleEntities&,
                       const ::epix::camera::Frustum&,
                       Opt<const ::epix::camera::RenderLayers&>,
                       Opt<const camera::MipBias&>,
                       Opt<const camera::TemporalJitter&>,
                       Opt<const view::Hdr&>,
                       Opt<const view::ColorGrading&>,
                       Opt<const ::epix::camera::Exposure&>,
                       Opt<const ::epix::camera::MainPassResolutionOverride&>,
                       const view::Msaa&,
                       const ::epix::camera::CameraMainTextureUsages&,
                       Opt<const ::epix::camera::Projection&>,
                       Opt<const view::NoIndirectDrawing&>>>> cameras,
    Res<batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    Extract<Query<const sync_world::RenderEntity&>> mapper,
    Extract<Query<Entity, With<::epix::window::PrimaryWindow, ::epix::window::Window>>> primary_window) {
    // extract camera entities to render world, this will spawn an related
    // entity with ExtractedCamera, ExtractedView and other components.

    auto primary = primary_window.single();

    for (auto&& [entity, camera, target, graph, gtransform, visible_entities, frustum, opt_render_layer, opt_mip_bias,
                 opt_temporal_jitter, opt_hdr, opt_color_grading, opt_exposure, opt_resolution_override, msaa,
                 main_texture_usages, opt_projection, opt_no_indirect_drawing] : cameras.iter()) {
        if (!camera.is_active) continue;
        const auto target_size   = camera.physical_target_size();
        const auto viewport_size = camera.physical_viewport_size();
        const auto viewport_rect = camera.physical_viewport_rect();
        if (!target_size || !viewport_size || !viewport_rect || target_size->x == 0 || target_size->y == 0) continue;
        auto normalized_target = target.normalize(primary);
        if (!normalized_target.has_value()) continue;
        const auto viewport_origin = viewport_rect->first;

        // Bevy represents HDR as a dedicated marker component.
        const bool hdr = opt_hdr.has_value();
        view::RenderVisibleEntities render_visible_entities;
        for (const auto& [visibility_class, main_entities] : visible_entities.entities) {
            auto& render_entities = render_visible_entities.entities[visibility_class];
            render_entities.reserve(main_entities.size());
            for (const Entity main_entity : main_entities) {
                const Entity render_entity =
                    mapper.get(main_entity)
                        .transform([](const std::reference_wrapper<const sync_world::RenderEntity>& re) {
                            return re.get().id();
                        })
                        .value_or(Entity::PLACEHOLDER);
                render_entities.emplace_back(render_entity, sync_world::MainEntity{main_entity});
            }
        }
        auto commands = cmd.spawn(epix::render::sync_world::TemporaryRenderEntity{});
        // single call to insert to reduce overhead
        commands.insert(
            ExtractedCamera{
                .target                 = *normalized_target,
                .physical_viewport_size = *viewport_size,
                .physical_target_size   = *target_size,
                .viewport               = camera.viewport,
                .render_graph           = static_cast<const graph::GraphLabel&>(graph),
                .order                  = camera.order,
                .clear_color            = camera.clear_color,
                .hdr                    = hdr,
                .exposure       = opt_exposure ? opt_exposure->get().exposure() : ::epix::camera::Exposure{}.exposure(),
                .output_mode    = camera.output_mode,
                .msaa_writeback = camera.msaa_writeback,
            },
            view::ExtractedView{
                .retained_view_entity =
                    view::RetainedViewEntity::create(sync_world::MainEntity{entity}, std::nullopt, 0),
                .clip_from_view  = camera.computed.clip_from_view,
                .world_from_view = gtransform,
                .hdr             = hdr,
                .viewport        = glm::uvec4(viewport_origin.x, viewport_origin.y, viewport_size->x, viewport_size->y),
                .invert_culling  = camera.invert_culling,
                .color_grading   = opt_color_grading ? opt_color_grading->get() : view::ColorGrading{},
            },
            std::move(render_visible_entities),
            // Bevy extracts render::view::Msaa via ExtractComponentPlugin.
            view::Msaa{msaa}, ::epix::camera::CameraMainTextureUsages{main_texture_usages},
            ::epix::camera::Frustum{frustum});
        // Bevy extracts MipBias only when the camera has one (ExtractComponentPlugin);
        // prepare_view_uniforms then falls back to 0.0 when absent.
        if (opt_mip_bias) {
            commands.insert(camera::MipBias{opt_mip_bias->get().bias});
        }
        if (opt_temporal_jitter) {
            commands.insert(camera::TemporalJitter{opt_temporal_jitter->get().offset});
        }
        if (opt_render_layer) {
            commands.insert(::epix::camera::RenderLayers{opt_render_layer->get()});
        }
        if (opt_projection) {
            commands.insert(::epix::camera::Projection{opt_projection->get()});
        }
        if (opt_resolution_override) {
            commands.insert(::epix::camera::MainPassResolutionOverride{opt_resolution_override->get().size});
        }
        if (opt_no_indirect_drawing || !gpu_preprocessing_support->is_culling_supported()) {
            commands.insert(view::NoIndirectDrawing{});
        }
    }
}

void epix::render::camera::CameraDriverNode::update(World& world) {
    if (!cameras) {
        cameras = world.try_query<Item<const ExtractedCamera&>>();
    } else {
        cameras->update_archetypes(world);
    }
}

std::expected<void, graph::NodeRunError> epix::render::camera::CameraDriverNode::run(graph::GraphContext& graph,
                                                                                     graph::RenderContext& render_ctx,
                                                                                     const World& world) {
    // Bevy CameraDriverNode owns this persistent query state instead of
    // recreating an ad-hoc query for every graph run.
    auto sorted = world.get_resource<SortedCameras>();
    auto windows = world.get_resource<window::ExtractedWindows>();
    if (!sorted || !windows || !cameras) return {};

    std::unordered_set<Entity> camera_windows;
    for (const auto& sorted_camera : sorted->get().cameras) {
        auto opt = cameras->query(world).get(sorted_camera.entity);
        if (!opt) continue;
        const auto& [camera] = *opt;

        bool run_graph = true;
        if (const auto* window_ref = camera.target
                                         .transform([](const auto& target) {
                                             return std::get_if<::epix::window::NormalizedWindowRef>(&target);
                                         })
                                         .value_or(nullptr)) {
            const auto window = windows->get().windows.find(window_ref->entity());
            if (window != windows->get().windows.end() && window->second.physical_width > 0 &&
                window->second.physical_height > 0) {
                camera_windows.insert(window_ref->entity());
            } else {
                run_graph = false;
            }
        }
        if (run_graph && !graph.run_sub_graph(camera.render_graph, {}, sorted_camera.entity)) {
            spdlog::warn("Failed to run camera render graph for entity {:#x}, with render graph label {}",
                         sorted_camera.entity.index,
                         camera.render_graph.type_index().short_name());
            return std::unexpected(graph::NodeRunError::RunSubGraphError);
        }
    }

    // Bevy also clears every acquired swapchain image that no camera graph
    // will touch. WGPU requires work before a presented acquired frame.
    const auto global_clear_color = world.get_resource<::epix::camera::ClearColor>();
    if (!global_clear_color) return {};
    for (const auto& [entity, window] : windows->get().windows) {
        if (camera_windows.contains(entity) && render_ctx.has_command_encoder()) continue;
        if (!window.swapchain_texture_view) continue;
        auto attachment = wgpu::RenderPassColorAttachment()
                              .setView(window.swapchain_texture_view)
                              .setDepthSlice(~0u)
                              .setLoadOp(wgpu::LoadOp::eClear)
                              .setStoreOp(wgpu::StoreOp::eStore)
                              .setClearValue(wgpu::Color(global_clear_color->get().r, global_clear_color->get().g,
                                                          global_clear_color->get().b, global_clear_color->get().a));
        auto pass = render_ctx.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(std::array{attachment}));
        pass.end();
    }
    return {};
}

void view::prepare_view_attachments(Res<window::ExtractedWindows> extracted_windows,
                                    Res<texture::ManualTextureViews> manual_texture_views,
                                    Query<Item<const camera::ExtractedCamera&>> cameras,
                                    ResMut<ViewTargetAttachments> view_target_attachments) {
    // Bevy view/mod.rs:1012-1039: prepare the default output once per target,
    // leaving a later extension (such as ScreenshotPlugin) free to override it.
    for (const auto& [extracted_camera] : cameras.iter()) {
        if (!extracted_camera.target) continue;
        const auto& target   = *extracted_camera.target;
        const auto target_id = target.identity();
        if (view_target_attachments->attachments.contains(target_id)) continue;
        auto texture_view =
            camera::NormalizedRenderTargetExt::get_texture_view(target, *extracted_windows, *manual_texture_views);
        auto texture_format = camera::NormalizedRenderTargetExt::get_texture_view_format(target, *extracted_windows,
                                                                                         *manual_texture_views);
        if (!texture_view || !*texture_view || !texture_format) continue;
        view_target_attachments->attachments.emplace(
            target_id, view::OutputColorAttachment::create(std::move(*texture_view), *texture_format));
    }
}
