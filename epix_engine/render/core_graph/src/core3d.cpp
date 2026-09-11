
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <epix/core_graph.hpp>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using namespace epix::render;
using namespace epix::core_graph::core_3d;
using namespace epix::ecs;
using namespace epix::app;

namespace {

/** @brief Number of samples a depth/transmission texture is created with. */
constexpr std::uint32_t kSingleSample = 1;

/** @brief Non-HDR target format (Bevy `TextureFormat::bevy_default`). */
constexpr wgpu::TextureFormat kBevyDefaultFormat = wgpu::TextureFormat::eRGBA8UnormSrgb;

/** @brief Cache key for the per-target, per-sample-count Core3D depth texture
 * (Bevy keys on `(camera.target, Msaa)`). */
struct DepthTextureKey {
    std::optional<::epix::camera::RenderTargetId> target;
    std::uint32_t sample_count = kSingleSample;

    bool operator==(const DepthTextureKey& other) const noexcept {
        return sample_count == other.sample_count && target == other.target;
    }
};

/** @brief Hasher for `DepthTextureKey`; direct wgpu has no `Msaa` hash. */
struct DepthTextureKeyHash {
    std::size_t operator()(const DepthTextureKey& key) const noexcept {
        std::size_t hash = key.target ? ::epix::camera::RenderTargetIdHash{}(*key.target) : std::size_t{0};
        hash ^= std::hash<std::uint32_t>{}(key.sample_count) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

/** @brief The optional render-target identity of a camera, used to share one
 * depth texture per output target per frame. */
std::optional<::epix::camera::RenderTargetId> render_target_of(const ::epix::render::camera::ExtractedCamera& camera) {
    return camera.target.transform([](const auto& value) { return value.identity(); });
}

std::size_t render_target_hash(const std::optional<::epix::camera::RenderTargetId>& target) {
    return target ? ::epix::camera::RenderTargetIdHash{}(*target) : std::size_t{0};
}

/** @brief Bevy's `GpuPreprocessingMode` selection for a 3D view: use culling
 * when the adapter supports it and the view allows indirect drawing. */
batching::GpuPreprocessingMode preprocessing_mode_for_view(const batching::GpuPreprocessingSupport& support,
                                                           bool no_indirect_drawing) noexcept {
    return support.min(no_indirect_drawing ? batching::GpuPreprocessingMode::PreprocessingOnly
                                           : batching::GpuPreprocessingMode::Culling);
}

}  // namespace

std::vector<std::pair<std::size_t, std::size_t>> epix::core_graph::core_3d::split_transmissive_range(
    std::size_t start, std::size_t end, std::size_t max_num_splits) {
    if (end <= start) {
        throw std::invalid_argument("To be split, a range must not be empty.");
    }
    if (max_num_splits == 0) {
        throw std::invalid_argument("max_num_splits must be at least 1.");
    }
    const std::size_t length     = end - start;
    const std::size_t num_splits = std::min(max_num_splits, length);
    const std::size_t step       = length / num_splits;
    std::size_t remainder        = length % num_splits;
    std::size_t cursor           = start;

    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    ranges.reserve(num_splits);
    for (std::size_t i = 0; i < num_splits; ++i) {
        // Bevy gives the remainder to the initial sub-ranges, one element each.
        const std::size_t extra = remainder > 0 ? (--remainder, std::size_t{1}) : std::size_t{0};
        const std::size_t stop  = std::min(cursor + step + extra, end);
        ranges.emplace_back(cursor, stop);
        cursor = stop;
    }
    return ranges;
}

void epix::core_graph::core_3d::extract_core_3d_camera_phases(
    ResMut<phase::ViewBinnedRenderPhases<Opaque3d>> opaque_phases,
    ResMut<phase::ViewBinnedRenderPhases<AlphaMask3d>> alpha_mask_phases,
    ResMut<phase::ViewSortedRenderPhases<Transmissive3d>> transmissive_phases,
    ResMut<phase::ViewSortedRenderPhases<Transparent3d>> transparent_phases,
    Extract<Query<Item<Entity, const ::epix::camera::Camera&, Has<::epix::render::view::NoIndirectDrawing>>,
                  With<::epix::camera::Camera3d>>> cameras,
    Local<std::unordered_set<::epix::render::view::RetainedViewEntity>> live_views,
    Res<batching::GpuPreprocessingSupport> gpu_preprocessing_support) {
    live_views->clear();
    for (auto&& [entity, camera, no_indirect_drawing] : cameras.iter()) {
        if (!camera.is_active) continue;
        const auto gpu_preprocessing_mode =
            preprocessing_mode_for_view(gpu_preprocessing_support.get(), no_indirect_drawing);
        // This is the main 3D camera, so use the first subview index (0).
        const auto retained_view = ::epix::render::view::RetainedViewEntity::create(
            ::epix::render::sync_world::MainEntity{entity}, std::nullopt, 0);
        opaque_phases->prepare_for_new_frame(retained_view, gpu_preprocessing_mode);
        alpha_mask_phases->prepare_for_new_frame(retained_view, gpu_preprocessing_mode);
        transmissive_phases->insert_or_clear(retained_view);
        transparent_phases->insert_or_clear(retained_view);
        live_views->insert(retained_view);
    }
    const auto remove_dead_views = [&live_views](const auto& entry) { return !live_views->contains(entry.first); };
    std::erase_if(opaque_phases->phases, remove_dead_views);
    std::erase_if(alpha_mask_phases->phases, remove_dead_views);
    std::erase_if(*transmissive_phases, remove_dead_views);
    std::erase_if(*transparent_phases, remove_dead_views);
}

void epix::core_graph::core_3d::prepare_core_3d_depth_textures(
    Commands cmd,
    ResMut<render::render_resource::TextureCache> texture_cache,
    Res<wgpu::Device> device,
    Res<phase::ViewBinnedRenderPhases<Opaque3d>> opaque_phases,
    Res<phase::ViewBinnedRenderPhases<AlphaMask3d>> alpha_mask_phases,
    Res<phase::ViewSortedRenderPhases<Transmissive3d>> transmissive_phases,
    Res<phase::ViewSortedRenderPhases<Transparent3d>> transparent_phases,
    Query<Item<Entity,
               const render::camera::ExtractedCamera&,
               const render::view::ExtractedView&,
               const ::epix::camera::Camera3d&,
               const render::view::Msaa&>> views) {
    // Bevy caches one depth texture per output target per frame, so cameras
    // compositing onto the same target share it. The usage is the union of
    // every camera writing that target.
    //
    // Bevy additionally adds COPY_SRC when a depth prepass is attached; the
    // prepass subsystem (C14) does not exist yet, so no view requests it.
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>, wgpu::TextureUsage, decltype(&render_target_hash)>
        render_target_usage{0, &render_target_hash};
    for (auto&& [entity, camera, extracted_view, camera_3d, msaa] : views.iter()) {
        (void)entity;
        (void)msaa;
        const auto& retained_view = extracted_view.retained_view_entity;
        if (!opaque_phases->phases.contains(retained_view) || !alpha_mask_phases->phases.contains(retained_view) ||
            !transmissive_phases->contains(retained_view) || !transparent_phases->contains(retained_view)) {
            continue;
        }
        const auto usage = camera_3d.depth_texture_usages.usage();
        auto& entry      = render_target_usage[render_target_of(camera)];
        entry            = entry | usage;
    }

    std::unordered_map<DepthTextureKey, render::render_resource::CachedTexture, DepthTextureKeyHash> textures;
    for (auto&& [entity, camera, extracted_view, camera_3d, msaa] : views.iter()) {
        const auto& retained_view = extracted_view.retained_view_entity;
        // Bevy reaches the same conclusion through its usage-map `expect`: a
        // view that is not a member of all four phases never has a target usage
        // entry, so it gets no depth texture. Epix skips it explicitly instead
        // of panicking.
        if (!opaque_phases->phases.contains(retained_view) || !alpha_mask_phases->phases.contains(retained_view) ||
            !transmissive_phases->contains(retained_view) || !transparent_phases->contains(retained_view)) {
            continue;
        }
        if (!camera.physical_target_size) continue;
        const auto size         = *camera.physical_target_size;
        const auto sample_count = render::view::samples(msaa);
        const auto target       = render_target_of(camera);
        auto it                 = textures.find(DepthTextureKey{target, sample_count});
        if (it == textures.end()) {
            const auto usage_it = render_target_usage.find(target);
            if (usage_it == render_target_usage.end()) continue;
            const auto usage = usage_it->second;
            wgpu::TextureDescriptor descriptor;
            descriptor.setLabel("view_depth_texture")
                .setSize({size.x, size.y, 1})
                .setMipLevelCount(1)
                .setSampleCount(sample_count)
                .setDimension(wgpu::TextureDimension::e2D)
                .setFormat(CORE_3D_DEPTH_FORMAT)
                .setUsage(usage);
            const render::render_resource::TextureCacheKey key{
                .format                = CORE_3D_DEPTH_FORMAT,
                .dimension             = wgpu::TextureDimension::e2D,
                .width                 = size.x,
                .height                = size.y,
                .depth_or_array_layers = 1,
                .mip_level_count       = 1,
                .sample_count          = sample_count,
                .usage                 = usage,
                .label                 = "view_depth_texture",
            };
            it = textures.emplace(DepthTextureKey{target, sample_count}, texture_cache->get(*device, key, descriptor))
                     .first;
        }
        // Camera3dDepthLoadOp::Clear supplies the reverse-Z clear value;
        // Load keeps whatever the depth target already holds.
        const auto* clear = std::get_if<::epix::camera::Camera3dDepthLoadOp::Clear>(&camera_3d.depth_load_op);
        const std::optional<float> clear_value = clear ? std::optional<float>{clear->value} : std::nullopt;
        cmd.entity(entity).insert(render::view::ViewDepthTexture::create(it->second, clear_value));
    }
}

void epix::core_graph::core_3d::prepare_core_3d_transmission_textures(
    Commands cmd,
    ResMut<render::render_resource::TextureCache> texture_cache,
    Res<wgpu::Device> device,
    Res<phase::ViewBinnedRenderPhases<Opaque3d>> opaque_phases,
    Res<phase::ViewBinnedRenderPhases<AlphaMask3d>> alpha_mask_phases,
    Res<phase::ViewSortedRenderPhases<Transmissive3d>> transmissive_phases,
    Res<phase::ViewSortedRenderPhases<Transparent3d>> transparent_phases,
    Query<Item<Entity,
               const render::camera::ExtractedCamera&,
               const ::epix::camera::Camera3d&,
               const render::view::ExtractedView&>> views) {
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>, render::render_resource::CachedTexture,
                       decltype(&render_target_hash)>
        textures{0, &render_target_hash};
    for (auto&& [entity, camera, camera_3d, view] : views.iter()) {
        const auto& retained_view = view.retained_view_entity;
        if (!opaque_phases->phases.contains(retained_view) || !alpha_mask_phases->phases.contains(retained_view) ||
            !transparent_phases->contains(retained_view)) {
            continue;
        }
        const auto transmissive_phase = transmissive_phases->find(retained_view);
        if (transmissive_phase == transmissive_phases->end()) continue;
        if (!camera.physical_target_size) continue;
        // Don't prepare a transmission texture if the number of steps is 0.
        if (camera_3d.screen_space_specular_transmission_steps == 0) continue;
        // Don't prepare one if there are no transmissive items to render.
        if (transmissive_phase->second.items.empty()) continue;

        const auto size   = *camera.physical_target_size;
        const auto target = render_target_of(camera);
        auto it           = textures.find(target);
        if (it == textures.end()) {
            const auto usage  = wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst;
            const auto format = view.hdr ? render::view::ViewTarget::TEXTURE_FORMAT_HDR : kBevyDefaultFormat;
            wgpu::TextureDescriptor descriptor;
            descriptor.setLabel("view_transmission_texture")
                .setSize({size.x, size.y, 1})
                .setMipLevelCount(1)
                // No need for MSAA, as we only copy the main texture here.
                .setSampleCount(kSingleSample)
                .setDimension(wgpu::TextureDimension::e2D)
                .setFormat(format)
                .setUsage(usage);
            const render::render_resource::TextureCacheKey key{
                .format                = format,
                .dimension             = wgpu::TextureDimension::e2D,
                .width                 = size.x,
                .height                = size.y,
                .depth_or_array_layers = 1,
                .mip_level_count       = 1,
                .sample_count          = kSingleSample,
                .usage                 = usage,
                .label                 = "view_transmission_texture",
            };
            it = textures.emplace(target, texture_cache->get(*device, key, descriptor)).first;
        }
        const auto sampler = device->createSampler(wgpu::SamplerDescriptor()
                                                       .setLabel("view_transmission_sampler")
                                                       .setMagFilter(wgpu::FilterMode::eLinear)
                                                       .setMinFilter(wgpu::FilterMode::eLinear)
                                                       // wgpu-native's C++ descriptor
                                                       // does not initialize this required
                                                       // field like Rust wgpu's Default does.
                                                       .setMaxAnisotropy(1));
        cmd.entity(entity).insert(ViewTransmissionTexture{
            .texture = it->second.texture, .view = it->second.default_view, .sampler = sampler});
    }
}

std::expected<void, graph::NodeRunError> MainOpaquePass3dNode::run(graph::GraphContext& graph,
                                                                   graph::RenderContext& render_context,
                                                                   typename ecs::QueryData<ViewQuery>::Item view,
                                                                   const World& world) const {
    auto&& [camera, extracted_view, target, depth] = view;
    const auto opaque_phases                       = world.get_resource<phase::ViewBinnedRenderPhases<Opaque3d>>();
    const auto alpha_mask_phases                   = world.get_resource<phase::ViewBinnedRenderPhases<AlphaMask3d>>();
    if (!opaque_phases || !alpha_mask_phases) return {};

    const auto opaque_phase     = opaque_phases->get().phases.find(extracted_view.retained_view_entity);
    const auto alpha_mask_phase = alpha_mask_phases->get().phases.find(extracted_view.retained_view_entity);
    if (opaque_phase == opaque_phases->get().phases.end() ||
        alpha_mask_phase == alpha_mask_phases->get().phases.end()) {
        return {};
    }

    const auto color_attachments     = std::array{target.get_color_attachment()};
    const auto depth_attachment      = depth.get_attachment(wgpu::StoreOp::eStore);
    const auto viewport              = camera.viewport;
    const auto view_entity           = graph.view_entity();
    const auto* world_ptr            = std::addressof(world);
    const auto* opaque_phase_ptr     = std::addressof(opaque_phase->second);
    const auto* alpha_mask_phase_ptr = std::addressof(alpha_mask_phase->second);
    render_context.add_command_buffer_generation_task([color_attachments, depth_attachment, viewport, view_entity,
                                                       world_ptr, opaque_phase_ptr,
                                                       alpha_mask_phase_ptr](wgpu::Device device) {
        auto encoder = device.createCommandEncoder(
            wgpu::CommandEncoderDescriptor().setLabel("main_opaque_pass_3d_command_encoder"));
        auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                       .setLabel("main_opaque_pass_3d")
                                                       .setColorAttachments(color_attachments)
                                                       .setDepthStencilAttachment(depth_attachment));
        if (viewport) {
            const auto& vp = *viewport;
            render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                    static_cast<float>(vp.physical_position.y), static_cast<float>(vp.physical_size.x),
                                    static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
        }
        if (!opaque_phase_ptr->is_empty()) opaque_phase_ptr->render(render_pass, *world_ptr, view_entity);
        if (!alpha_mask_phase_ptr->is_empty()) alpha_mask_phase_ptr->render(render_pass, *world_ptr, view_entity);
        render_pass.end();
        return encoder.finish();
    });
    return {};
}

std::expected<void, graph::NodeRunError> MainTransmissivePass3dNode::run(graph::GraphContext& graph,
                                                                         graph::RenderContext& render_context,
                                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                                         const World& world) const {
    auto&& [camera, extracted_view, camera_3d, target, transmission, depth] = view;
    const auto transmissive_phases = world.get_resource<phase::ViewSortedRenderPhases<Transmissive3d>>();
    if (!transmissive_phases) return {};
    const auto transmissive_phase = transmissive_phases->get().find(extracted_view.retained_view_entity);
    if (transmissive_phase == transmissive_phases->get().end()) return {};
    if (transmissive_phase->second.items.empty()) return {};
    if (!camera.physical_target_size) return {};

    const auto view_entity = graph.view_entity();
    const auto viewport    = camera.viewport;
    const auto size        = *camera.physical_target_size;
    const auto steps       = camera_3d.screen_space_specular_transmission_steps;
    const auto* world_ptr  = std::addressof(world);
    const auto* phase_ptr  = std::addressof(transmissive_phase->second);

    const auto color_attachments = std::array{target.get_color_attachment()};
    const auto depth_attachment  = depth.get_attachment(wgpu::StoreOp::eStore);

    // Bevy splits the depth-sorted transmissive items into at most `steps`
    // back-to-front ranges and copies the main texture into the transmission
    // texture before each range. Epix records the copies and the passes in one
    // deferred generated command buffer so the per-step ordering is preserved.
    const auto transmission_texture =
        transmission.transform([](const ViewTransmissionTexture& value) { return value.texture; });
    if (steps > 0 && !transmission_texture) {
        // Bevy expects the texture to exist here; Epix returns instead of
        // panicking when the prepare system skipped this view.
        return {};
    }
    const auto main_texture = target.main_texture();
    render_context.add_command_buffer_generation_task([color_attachments, depth_attachment, viewport, size, steps,
                                                       view_entity, world_ptr, phase_ptr, transmission_texture,
                                                       main_texture](wgpu::Device device) {
        auto encoder = device.createCommandEncoder(
            wgpu::CommandEncoderDescriptor().setLabel("main_transmissive_pass_3d_command_encoder"));
        // A null range draws every retained item in one pass, matching the
        // zero-step path of Bevy's transmissive node.
        const auto render_range = [&](std::optional<std::pair<std::size_t, std::size_t>> range) {
            auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                           .setLabel("main_transmissive_pass_3d")
                                                           .setColorAttachments(color_attachments)
                                                           .setDepthStencilAttachment(depth_attachment));
            if (viewport) {
                const auto& vp = *viewport;
                render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                        static_cast<float>(vp.physical_position.y),
                                        static_cast<float>(vp.physical_size.x), static_cast<float>(vp.physical_size.y),
                                        vp.depth.first, vp.depth.second);
            }
            if (range) {
                phase_ptr->render_range(render_pass, *world_ptr, view_entity, range->first, range->second);
            } else {
                phase_ptr->render(render_pass, *world_ptr, view_entity);
            }
            render_pass.end();
        };

        if (steps > 0) {
            for (const auto& range : split_transmissive_range(0, phase_ptr->items.size(), steps)) {
                encoder.copyTextureToTexture(wgpu::TexelCopyTextureInfo()
                                                 .setTexture(main_texture)
                                                 .setMipLevel(0)
                                                 .setOrigin(wgpu::Origin3D{0, 0, 0}),
                                             wgpu::TexelCopyTextureInfo()
                                                 .setTexture(*transmission_texture)
                                                 .setMipLevel(0)
                                                 .setOrigin(wgpu::Origin3D{0, 0, 0}),
                                             wgpu::Extent3D{size.x, size.y, 1});
                render_range(range);
            }
        } else {
            render_range(std::nullopt);
        }
        return encoder.finish();
    });
    return {};
}

std::expected<void, graph::NodeRunError> MainTransparentPass3dNode::run(graph::GraphContext& graph,
                                                                        graph::RenderContext& render_context,
                                                                        typename ecs::QueryData<ViewQuery>::Item view,
                                                                        const World& world) const {
    auto&& [camera, extracted_view, target, depth] = view;
    const auto transparent_phases                  = world.get_resource<phase::ViewSortedRenderPhases<Transparent3d>>();
    if (!transparent_phases) return {};
    const auto transparent_phase = transparent_phases->get().find(extracted_view.retained_view_entity);
    if (transparent_phase == transparent_phases->get().end()) return {};

    const auto color_attachments      = std::array{target.get_color_attachment()};
    const auto depth_attachment       = depth.get_attachment(wgpu::StoreOp::eStore);
    const auto viewport               = camera.viewport;
    const auto view_entity            = graph.view_entity();
    const auto* world_ptr             = std::addressof(world);
    const auto* transparent_phase_ptr = std::addressof(transparent_phase->second);
    render_context.add_command_buffer_generation_task([color_attachments, depth_attachment, viewport, view_entity,
                                                       world_ptr, transparent_phase_ptr](wgpu::Device device) {
        auto encoder =
            device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("main_transparent_pass_3d"));
        // Bevy's 3D transparent node only begins its pass when the retained
        // sorted phase has work, unlike Core2D whose pass must always run to
        // clear the background color.
        if (transparent_phase_ptr->items.empty()) return encoder.finish();
        auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                       .setLabel("main_transparent_pass_3d")
                                                       .setColorAttachments(color_attachments)
                                                       .setDepthStencilAttachment(depth_attachment));
        if (viewport) {
            const auto& vp = *viewport;
            render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                    static_cast<float>(vp.physical_position.y), static_cast<float>(vp.physical_size.x),
                                    static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
        }
        transparent_phase_ptr->render(render_pass, *world_ptr, view_entity);
        render_pass.end();
        return encoder.finish();
    });
    return {};
}

void Core3dGraph::add_to(graph::RenderGraph& g, World& world) {
    spdlog::debug("[render.core_graph] Adding Core3D sub-graph to render graph.");
    graph::RenderGraph g3d;
    // Bevy registers real prepass/deferred nodes at these labels; those
    // subsystems (C14/C15) do not exist yet, so the labels are installed as
    // empty nodes. This keeps Bevy's extension points and edge order instead
    // of deleting them.
    g3d.add_node(Core3dNodes::EarlyPrepass, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::LatePrepass, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::EarlyDeferredPrepass, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::LateDeferredPrepass, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::CopyDeferredLightingId, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::EndPrepasses, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::StartMainPass, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::MainOpaquePass, graph::ViewNodeRunner{MainOpaquePass3dNode{}, world});
    g3d.add_node(Core3dNodes::MainTransmissivePass, graph::ViewNodeRunner{MainTransmissivePass3dNode{}, world});
    g3d.add_node(Core3dNodes::MainTransparentPass, graph::ViewNodeRunner{MainTransparentPass3dNode{}, world});
    g3d.add_node(Core3dNodes::EndMainPass, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::StartMainPassPostProcessing, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::Tonemapping, graph::ViewNodeRunner{TonemappingNode{}, world});
    g3d.add_node(Core3dNodes::EndMainPassPostProcessing, graph::EmptyNode{});
    g3d.add_node(Core3dNodes::Upscaling, graph::ViewNodeRunner{UpscalingNode{}, world});
    g3d.add_node_edges(Core3dNodes::EarlyPrepass, Core3dNodes::EarlyDeferredPrepass, Core3dNodes::LatePrepass,
                       Core3dNodes::LateDeferredPrepass, Core3dNodes::CopyDeferredLightingId, Core3dNodes::EndPrepasses,
                       Core3dNodes::StartMainPass, Core3dNodes::MainOpaquePass, Core3dNodes::MainTransmissivePass,
                       Core3dNodes::MainTransparentPass, Core3dNodes::EndMainPass,
                       Core3dNodes::StartMainPassPostProcessing, Core3dNodes::Tonemapping,
                       Core3dNodes::EndMainPassPostProcessing, Core3dNodes::Upscaling);
    g.add_sub_graph(Core3d, std::move(g3d));
}

void Core3dPlugin::attach(App& app) {
    // Mirror Core3dPlugin::build: Camera3d gets the Core3D graph plus the
    // component defaults that define a plain 3D camera output path.
    app.world_mut().register_required_components_with<::epix::camera::Camera3d>([] { return DebandDither::Enabled; });
    app.world_mut().register_required_components_with<::epix::camera::Camera3d>(
        [] { return render::camera::CameraRenderGraph{Core3d}; });
    app.world_mut().register_required_components_with<::epix::camera::Camera3d>([] { return Tonemapping{}; });
    app.add_plugins(render::ExtractComponentPlugin<::epix::camera::Camera3d>{});
    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().init_resource<phase::DrawFunctions<Opaque3d>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<AlphaMask3d>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<Transmissive3d>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<Transparent3d>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<Opaque3d>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<AlphaMask3d>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transmissive3d>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transparent3d>>();
        Core3d.add_to(render_app.resource_mut<graph::RenderGraph>(), render_app.world_mut());

        render_app.add_systems(Render, into(phase::sort_phase_system<Transmissive3d>)
                                           .in_set(RenderSystems::PhaseSort)
                                           .set_name("sort transmissive 3d phase"));
        render_app.add_systems(Render, into(phase::sort_phase_system<Transparent3d>)
                                           .in_set(RenderSystems::PhaseSort)
                                           .set_name("sort transparent 3d phase"));
        render_app.add_systems(Render, into(prepare_core_3d_depth_textures)
                                           .in_set(RenderSystems::PrepareResources)
                                           .set_name("prepare core 3d depth textures"));
        render_app.add_systems(Render, into(prepare_core_3d_transmission_textures)
                                           .in_set(RenderSystems::PrepareResources)
                                           .set_name("prepare core 3d transmission textures"));
        render_app.add_systems(ExtractSchedule,
                               into(extract_core_3d_camera_phases).set_name("extract core 3d camera phases"));
        return std::make_optional(std::ref(render_app));
    });
}
