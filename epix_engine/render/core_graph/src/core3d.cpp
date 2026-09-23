
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

void epix::core_graph::core_3d::check_msaa(Query<Item<Mut<render::view::Msaa>>,
                                                   With<::epix::camera::Camera, DeferredPrepass>> deferred_views) {
    // Bevy `check_msaa` (core_3d/mod.rs:978-988): MSAA is incompatible with
    // deferred rendering, so every deferred view is forced to `Msaa::Off` with a
    // warning unless it was already off.
    for (auto&& [msaa] : deferred_views.iter()) {
        if (msaa.get() == render::view::Msaa::Off) continue;
        spdlog::warn("[core_graph] MSAA is incompatible with deferred rendering and has been disabled.");
        msaa.get_mut() = render::view::Msaa::Off;
    }
}

void epix::core_graph::core_3d::configure_occlusion_culling_view_targets(
    Query<Item<Mut<::epix::camera::Camera3d>>,
          Filter<With<render::experimental::OcclusionCulling>, Without<render::view::NoIndirectDrawing>,
                 With<DepthPrepass>>>
        view_targets) {
    // Bevy `configure_occlusion_culling_view_targets` (core_3d/mod.rs:960-975):
    // the occlusion-culling pass samples the depth texture, so a view that runs
    // it needs `TEXTURE_BINDING` on top of whatever it already asked for.
    for (auto&& [camera_3d] : view_targets.iter()) {
        auto& usage = camera_3d.get_mut().depth_texture_usages;
        usage.bits |= static_cast<std::uint32_t>(wgpu::TextureUsage::eTextureBinding);
    }
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
               const render::view::Msaa&,
               ecs::Has<DepthPrepass>>> views) {
    // Bevy caches one depth texture per output target per frame, so cameras
    // compositing onto the same target share it. The usage is the union of
    // every camera writing that target.
    //
    // Bevy additionally adds COPY_SRC when a depth prepass is attached, because
    // the prepass node copies the view depth texture into the prepass depth
    // texture (C14, `prepass/node.rs:244-249`). That is honoured here, which
    // closes the C13-D5 divergence.
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>, wgpu::TextureUsage, decltype(&render_target_hash)>
        render_target_usage{0, &render_target_hash};
    for (auto&& [entity, camera, extracted_view, camera_3d, msaa, depth_prepass] : views.iter()) {
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
        if (depth_prepass) entry = entry | wgpu::TextureUsage::eCopySrc;
    }

    std::unordered_map<DepthTextureKey, render::render_resource::CachedTexture, DepthTextureKeyHash> textures;
    for (auto&& [entity, camera, extracted_view, camera_3d, msaa, depth_prepass] : views.iter()) {
        (void)depth_prepass;
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
        // Bevy `SamplerDescriptor { label, mag_filter: Linear, min_filter: Linear,
        // ..Default::default() }` (`bevy_core_pipeline/src/core_3d/mod.rs:941-946`).
        // The rest of the descriptor comes from the wrapper's default
        // constructor, which applies wgpu-native's `WGPU_SAMPLER_DESCRIPTOR_INIT`.
        const auto sampler = device->createSampler(wgpu::SamplerDescriptor()
                                                       .setLabel("view_transmission_sampler")
                                                       .setMagFilter(wgpu::FilterMode::eLinear)
                                                       .setMinFilter(wgpu::FilterMode::eLinear));
        cmd.entity(entity).insert(ViewTransmissionTexture{
            .texture = it->second.texture, .view = it->second.default_view, .sampler = sampler});
    }
}

std::expected<void, graph::NodeRunError> MainOpaquePass3dNode::run(graph::GraphContext& graph,
                                                                   graph::RenderContext& render_context,
                                                                   typename ecs::QueryData<ViewQuery>::Item view,
                                                                   const World& world) const {
    auto&& [camera, extracted_view, target, depth, view_uniform_offset, skybox_pipeline_id, skybox_bind_group,
            resolution_override] = view;
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
    // Bevy `Viewport::from_viewport_and_override(camera.viewport.as_ref(),
    // resolution_override)` (`main_opaque_pass_3d_node.rs:95-99`).
    const auto viewport = ::epix::camera::Viewport::from_viewport_and_override(
        camera.viewport, resolution_override.transform([](const auto& value) { return value.get().size; }));
    const auto view_entity           = graph.view_entity();
    const auto* world_ptr            = std::addressof(world);
    const auto* opaque_phase_ptr     = std::addressof(opaque_phase->second);
    const auto* alpha_mask_phase_ptr = std::addressof(alpha_mask_phase->second);
    // Bevy draws the skybox after the two binned phases, from the view's
    // specialized pipeline and its bind group with both dynamic offsets
    // (`main_opaque_pass_3d_node.rs:119-133`).
    const auto skybox_pipeline = skybox_pipeline_id ? std::optional{skybox_pipeline_id->get().pipeline_id}
                                                   : std::nullopt;
    const auto skybox_group    = skybox_bind_group
                                     ? std::optional{std::pair{skybox_bind_group->get().bind_group,
                                                               skybox_bind_group->get().uniform_offset}}
                                     : std::nullopt;
    render_context.add_command_buffer_generation_task(
        [color_attachments, depth_attachment, viewport, view_entity, world_ptr, opaque_phase_ptr,
         alpha_mask_phase_ptr, view_uniform_offset, skybox_pipeline, skybox_group](wgpu::Device device) {
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

            // Skybox draw using a fullscreen triangle.
            if (skybox_pipeline && skybox_group) {
                const auto pipeline_server = world_ptr->get_resource<render::PipelineServer>();
                const auto pipeline =
                    pipeline_server
                        ? pipeline_server->get().get_render_pipeline(*skybox_pipeline)
                        : std::expected<std::reference_wrapper<const render::RenderPipeline>,
                                        render::GetPipelineError>{
                              std::unexpect, render::GetPipelineError{}};
                if (pipeline) {
                    render_pass.setPipeline(pipeline->get().pipeline());
                    const std::array offsets{view_uniform_offset.offset, skybox_group->second};
                    render_pass.setBindGroup(0, skybox_group->first, offsets);
                    render_pass.draw(3, 1, 0, 0);
                }
            }

            render_pass.end();
            return encoder.finish();
        });
    return {};
}

std::expected<void, graph::NodeRunError> MainTransmissivePass3dNode::run(graph::GraphContext& graph,
                                                                         graph::RenderContext& render_context,
                                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                                         const World& world) const {
    auto&& [camera, extracted_view, camera_3d, target, transmission, depth, resolution_override] = view;
    const auto transmissive_phases = world.get_resource<phase::ViewSortedRenderPhases<Transmissive3d>>();
    if (!transmissive_phases) return {};
    const auto transmissive_phase = transmissive_phases->get().find(extracted_view.retained_view_entity);
    if (transmissive_phase == transmissive_phases->get().end()) return {};
    if (transmissive_phase->second.items.empty()) return {};
    if (!camera.physical_target_size) return {};

    const auto view_entity = graph.view_entity();
    // Bevy uses the raw camera viewport for the stepped passes and
    // `Viewport::from_viewport_and_override` on the zero-step path
    // (`main_transmissive_pass_3d_node.rs:104-106,123-128`).
    const auto stepped_viewport = camera.viewport;
    const auto plain_viewport   = ::epix::camera::Viewport::from_viewport_and_override(
        camera.viewport, resolution_override.transform([](const auto& value) { return value.get().size; }));
    const auto size       = *camera.physical_target_size;
    const auto steps      = camera_3d.screen_space_specular_transmission_steps;
    const auto* world_ptr = std::addressof(world);
    const auto* phase_ptr = std::addressof(transmissive_phase->second);

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
    render_context.add_command_buffer_generation_task([color_attachments, depth_attachment, stepped_viewport,
                                                       plain_viewport, size, steps, view_entity, world_ptr, phase_ptr,
                                                       transmission_texture, main_texture](wgpu::Device device) {
        auto encoder = device.createCommandEncoder(
            wgpu::CommandEncoderDescriptor().setLabel("main_transmissive_pass_3d_command_encoder"));
        // A null range draws every retained item in one pass, matching the
        // zero-step path of Bevy's transmissive node.
        const auto render_range = [&](std::optional<std::pair<std::size_t, std::size_t>> range) {
            auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                           .setLabel("main_transmissive_pass_3d")
                                                           .setColorAttachments(color_attachments)
                                                           .setDepthStencilAttachment(depth_attachment));
            const auto& viewport = range ? stepped_viewport : plain_viewport;
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
    auto&& [camera, extracted_view, target, depth, resolution_override] = view;
    const auto transparent_phases                  = world.get_resource<phase::ViewSortedRenderPhases<Transparent3d>>();
    if (!transparent_phases) return {};
    const auto transparent_phase = transparent_phases->get().find(extracted_view.retained_view_entity);
    if (transparent_phase == transparent_phases->get().end()) return {};

    const auto color_attachments      = std::array{target.get_color_attachment()};
    const auto depth_attachment       = depth.get_attachment(wgpu::StoreOp::eStore);
    const auto viewport               = ::epix::camera::Viewport::from_viewport_and_override(
        camera.viewport, resolution_override.transform([](const auto& value) { return value.get().size; }));
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

// C14 prepass. Mirrors Bevy's `prepass/node.rs` and the prepass half of
// `core_3d/mod.rs`; the recorded adaptations are the missing skybox inputs and
// the absersent occlusion-culling split, both of which are other rows.

std::optional<wgpu::TextureView> ViewPrepassTextures::depth_view() const {
    if (!depth) return std::nullopt;
    return depth->texture.default_view;
}

std::optional<wgpu::TextureView> ViewPrepassTextures::normal_view() const {
    if (!normal) return std::nullopt;
    return normal->texture.default_view;
}

std::optional<wgpu::TextureView> ViewPrepassTextures::motion_vectors_view() const {
    if (!motion_vectors) return std::nullopt;
    return motion_vectors->texture.default_view;
}

std::optional<wgpu::TextureView> ViewPrepassTextures::deferred_view() const {
    if (!deferred) return std::nullopt;
    return deferred->texture.default_view;
}

namespace {
/** @brief Everything the prepass skybox draw needs, gathered from one view.
 *  Bevy's prepass node carries these as query items (`prepass/node.rs:215-235`). */
struct SkyboxPrepassDraw {
    ::epix::render::CachedPipelineId pipeline_id;
    wgpu::BindGroup bind_group;
    /** @brief This view's `ViewUniform` entry offset (the first dynamic offset). */
    std::uint32_t view_uniform_offset = 0;
    /** @brief This view's `PreviousViewData` entry offset (the second). */
    std::uint32_t previous_view_uniform_offset = 0;
};

/** @brief Shared body of Bevy's `run_prepass` (`prepass/node.rs:107`). */
std::expected<void, graph::NodeRunError> run_prepass(graph::GraphContext& graph,
                                                     graph::RenderContext& render_context,
                                                     const ::epix::render::camera::ExtractedCamera& camera,
                                                     const ::epix::render::view::ExtractedView& extracted_view,
                                                     const ::epix::render::view::ViewDepthTexture& view_depth_texture,
                                                     const ViewPrepassTextures& view_prepass_textures,
                                                     bool has_deferred,
                                                     std::optional<glm::uvec2> resolution_override,
                                                     const World& world,
                                                     std::string_view label) {
    // A deferred prepass replaces this one entirely, so a deferred view leaves
    // the bins empty; bail out instead of looking at them (Bevy node.rs:127).
    if (has_deferred) return {};

    // The skybox's motion-vector draw (Bevy `prepass/node.rs:215-235`). Bevy reads
    // these off the view's query; looking them up here keeps the node queries
    // untouched, and the draw runs only when all four exist, which is the
    // condition Bevy's `if let` expresses.
    const auto skybox_prepass_draw = [&]() -> std::optional<SkyboxPrepassDraw> {
        const auto entity          = graph.view_entity();
        const auto pipeline        = world.entity(entity).get<::epix::core_graph::RenderSkyboxPrepassPipeline>();
        const auto bind_group      = world.entity(entity).get<::epix::core_graph::SkyboxPrepassBindGroup>();
        const auto view_offset     = world.entity(entity).get<::epix::render::view::ViewUniformOffset>();
        const auto previous_offset = world.entity(entity).get<::epix::core_graph::PreviousViewUniformOffset>();
        if (!pipeline || !bind_group || !view_offset || !previous_offset) return std::nullopt;
        return SkyboxPrepassDraw{.pipeline_id                  = pipeline->get().pipeline_id,
                                 .bind_group                   = bind_group->get().bind_group,
                                 .view_uniform_offset          = view_offset->get().offset,
                                 .previous_view_uniform_offset = previous_offset->get().offset};
    }();

    const auto opaque_prepass_phases = world.get_resource<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>();
    const auto alpha_mask_prepass_phases = world.get_resource<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>>();
    if (!opaque_prepass_phases || !alpha_mask_prepass_phases) return {};

    const auto opaque_phase = opaque_prepass_phases->get().phases.find(extracted_view.retained_view_entity);
    const auto alpha_mask_phase = alpha_mask_prepass_phases->get().phases.find(extracted_view.retained_view_entity);
    if (opaque_phase == opaque_prepass_phases->get().phases.end() ||
        alpha_mask_phase == alpha_mask_prepass_phases->get().phases.end()) {
        return {};
    }

    // Bevy's attachment order: normals, motion vectors, then the two deferred
    // attachments it fills with `None` here. wgpu's C descriptor takes
    // `RenderPassColorAttachment` values directly (there is no per-slot None),
    // so the two absent slots are filled with default-constructed attachments;
    // they are only ever reached when at least one real attachment is present.
    std::vector<wgpu::RenderPassColorAttachment> color_attachments;
    color_attachments.push_back(view_prepass_textures.normal
                                    ? view_prepass_textures.normal->get_attachment()
                                    : wgpu::RenderPassColorAttachment{});
    color_attachments.push_back(view_prepass_textures.motion_vectors
                                    ? view_prepass_textures.motion_vectors->get_attachment()
                                    : wgpu::RenderPassColorAttachment{});
    color_attachments.push_back(wgpu::RenderPassColorAttachment{});
    color_attachments.push_back(wgpu::RenderPassColorAttachment{});

    // When every attachment is absent there is no fragment stage at all, so the
    // color attachment list is emptied rather than filled with empty entries.
    const bool has_any_color =
        view_prepass_textures.normal.has_value() || view_prepass_textures.motion_vectors.has_value();

    const auto depth_attachment = view_depth_texture.get_attachment(wgpu::StoreOp::eStore);
    // Bevy `Viewport::from_viewport_and_override(camera.viewport.as_ref(),
    // resolution_override)` (`prepass/node.rs:190-194`).
    const auto viewport    = ::epix::camera::Viewport::from_viewport_and_override(camera.viewport, resolution_override);
    const auto view_entity = graph.view_entity();
    const auto* world_ptr       = std::addressof(world);
    const auto* opaque_ptr      = std::addressof(opaque_phase->second);
    const auto* mask_ptr        = std::addressof(alpha_mask_phase->second);
    const auto has_prepass_depth = view_prepass_textures.depth.has_value();
    const auto prepass_depth_texture =
        has_prepass_depth ? std::optional<wgpu::Texture>{view_prepass_textures.depth->texture.texture} : std::nullopt;
    const auto prepass_copy_size  = view_prepass_textures.size;
    const auto depth_texture_copy = view_depth_texture.texture;

    render_context.add_command_buffer_generation_task(
        [color_attachments, has_any_color, depth_attachment, viewport, view_entity, world_ptr, opaque_ptr, mask_ptr,
         has_prepass_depth, prepass_depth_texture, prepass_copy_size, depth_texture_copy, skybox_prepass_draw,
         label](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(
                wgpu::CommandEncoderDescriptor().setLabel("prepass_command_encoder"));

            {
                wgpu::RenderPassDescriptor descriptor;
                descriptor.setLabel(label.data());
                if (has_any_color) descriptor.setColorAttachments(color_attachments);
                descriptor.setDepthStencilAttachment(depth_attachment);
                auto render_pass = encoder.beginRenderPass(descriptor);
                if (viewport) {
                    const auto& vp = *viewport;
                    render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                            static_cast<float>(vp.physical_position.y),
                                            static_cast<float>(vp.physical_size.x),
                                            static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
                }
                if (!opaque_ptr->is_empty()) opaque_ptr->render(render_pass, *world_ptr, view_entity);
                if (!mask_ptr->is_empty()) mask_ptr->render(render_pass, *world_ptr, view_entity);

                // Skybox draw using a fullscreen triangle, after the phase items,
                // with both dynamic offsets (Bevy `prepass/node.rs:215-235`).
                if (skybox_prepass_draw) {
                    const auto pipeline_server = world_ptr->get_resource<::epix::render::PipelineServer>();
                    const auto pipeline =
                        pipeline_server
                            ? pipeline_server->get().get_render_pipeline(skybox_prepass_draw->pipeline_id)
                            : std::expected<std::reference_wrapper<const ::epix::render::RenderPipeline>,
                                            ::epix::render::GetPipelineError>{std::unexpect,
                                                                              ::epix::render::GetPipelineError{}};
                    if (pipeline) {
                        render_pass.setPipeline(pipeline->get().pipeline());
                        const std::array offsets{skybox_prepass_draw->view_uniform_offset,
                                                 skybox_prepass_draw->previous_view_uniform_offset};
                        render_pass.setBindGroup(0, skybox_prepass_draw->bind_group, offsets);
                        render_pass.draw(3, 1, 0, 0);
                    }
                }

                render_pass.end();
            }

            // Bevy copies the view depth texture into the prepass depth texture
            // after the pass when no deferred prepass will do it.
            if (has_prepass_depth && prepass_depth_texture) {
                auto source          = wgpu::TexelCopyTextureInfo();
                source.texture       = depth_texture_copy;
                source.mipLevel      = 0;
                source.origin        = wgpu::Origin3D{0, 0, 0};
                source.aspect        = wgpu::TextureAspect::eAll;
                auto destination     = wgpu::TexelCopyTextureInfo();
                destination.texture  = *prepass_depth_texture;
                destination.mipLevel = 0;
                destination.origin   = wgpu::Origin3D{0, 0, 0};
                destination.aspect   = wgpu::TextureAspect::eAll;
                encoder.copyTextureToTexture(source, destination, prepass_copy_size);
            }

            return encoder.finish();
        });
    return {};
}
}  // namespace

std::expected<void, graph::NodeRunError> EarlyPrepassNode::run(graph::GraphContext& graph,
                                                              graph::RenderContext& render_context,
                                                              typename ecs::QueryData<ViewQuery>::Item view,
                                                              const World& world) const {
    auto&& [camera, extracted_view, depth_texture, prepass_textures, uniform_offset, has_deferred,
            resolution_override] = view;
    static_cast<void>(uniform_offset);
    return run_prepass(graph, render_context, camera, extracted_view, depth_texture, prepass_textures, has_deferred,
                       resolution_override.transform([](const auto& value) { return value.get().size; }), world,
                       "early prepass");
}

std::expected<void, graph::NodeRunError> LatePrepassNode::run(graph::GraphContext& graph,
                                                              graph::RenderContext& render_context,
                                                              typename ecs::QueryData<ViewQuery>::Item view,
                                                              const World& world) const {
    // Bevy only needs a late prepass when occlusion culling is active and the
    // view allows indirect drawing (prepass/node.rs:87-92). The `OcclusionCulling`
    // component exists in Epix but nothing yet splits the bins into a
    // previously-visible early set and a newly-visible late set, so a late pass
    // would draw the early bins a second time. The early node owns the whole
    // prepass until that splitter lands (C14-D2); its other prerequisite, the
    // previous frame's view data, is now produced (`update_previous_view_data`
    // and `prepare_previous_view_uniforms`).
    static_cast<void>(graph);
    static_cast<void>(render_context);
    static_cast<void>(view);
    static_cast<void>(world);
    return {};
}

void epix::core_graph::core_3d::extract_camera_prepass_phase(
    Commands cmd,
    ResMut<::epix::render::phase::ViewBinnedRenderPhases<Opaque3dPrepass>> opaque_prepass_phases,
    ResMut<::epix::render::phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>> alpha_mask_prepass_phases,
    ResMut<::epix::render::phase::ViewBinnedRenderPhases<Opaque3dDeferred>> opaque_deferred_phases,
    ResMut<::epix::render::phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>> alpha_mask_deferred_phases,
    Extract<Query<Item<Entity,
                       ::epix::render::sync_world::RenderEntity,
                       const ::epix::camera::Camera&,
                       Has<::epix::render::view::NoIndirectDrawing>,
                       Has<DepthPrepass>,
                       Has<NormalPrepass>,
                       Has<MotionVectorPrepass>,
                       Has<DeferredPrepass>>,
                 With<::epix::camera::Camera3d>>> cameras,
    Local<std::unordered_set<::epix::render::view::RetainedViewEntity>> live_views,
    Res<::epix::render::batching::GpuPreprocessingSupport> gpu_preprocessing_support) {
    // Bevy `extract_camera_prepass_phase` (core_3d/mod.rs:665-788): one system
    // handles both the prepass and the deferred phases. An active camera that
    // asks for a depth, normal, or motion-vector prepass prepares both binned
    // prepass phases for the new frame, an active camera with a
    // `DeferredPrepass` prepares both binned deferred phases, and the other
    // branch removes the corresponding phases. Every active camera is live
    // regardless of which phases it asked for. The preprocessing mode is the
    // same `GpuPreprocessingSupport` decision the main 3D phases use, so the
    // extractions agree by construction.
    live_views->clear();
    for (auto&& [entity, render_entity, camera, no_indirect_drawing, depth_prepass, normal_prepass, motion_vector_prepass,
                 deferred_prepass] : cameras.iter()) {
        if (!camera.is_active) continue;
        const auto gpu_preprocessing_mode =
            preprocessing_mode_for_view(gpu_preprocessing_support.get(), no_indirect_drawing);
        // This is the main 3D camera, so use the first subview index (0).
        const auto retained_view =
            ::epix::render::view::RetainedViewEntity::create(::epix::render::sync_world::MainEntity{entity},
                                                             std::nullopt, 0);
        if (depth_prepass || normal_prepass || motion_vector_prepass) {
            opaque_prepass_phases->prepare_for_new_frame(retained_view, gpu_preprocessing_mode);
            alpha_mask_prepass_phases->prepare_for_new_frame(retained_view, gpu_preprocessing_mode);
        } else {
            opaque_prepass_phases->phases.erase(retained_view);
            alpha_mask_prepass_phases->phases.erase(retained_view);
        }
        if (deferred_prepass) {
            opaque_deferred_phases->prepare_for_new_frame(retained_view, gpu_preprocessing_mode);
            alpha_mask_deferred_phases->prepare_for_new_frame(retained_view, gpu_preprocessing_mode);
        } else {
            opaque_deferred_phases->phases.erase(retained_view);
            alpha_mask_deferred_phases->phases.erase(retained_view);
        }
        live_views->insert(retained_view);

        // Bevy re-inserts (or removes) the prepass markers on the synced render
        // entity, which is how the render-world `prepare_prepass_textures` and
        // the prepass/deferred nodes observe them (core_3d/mod.rs:744-782).
        // Bevy's two double-buffer markers have no Epix counterpart yet.
        auto camera_commands = cmd.entity(render_entity.id());
        if (depth_prepass) {
            camera_commands.insert(DepthPrepass{});
        } else {
            camera_commands.remove<DepthPrepass>();
        }
        if (normal_prepass) {
            camera_commands.insert(NormalPrepass{});
        } else {
            camera_commands.remove<NormalPrepass>();
        }
        if (motion_vector_prepass) {
            camera_commands.insert(MotionVectorPrepass{});
        } else {
            camera_commands.remove<MotionVectorPrepass>();
        }
        if (deferred_prepass) {
            camera_commands.insert(DeferredPrepass{});
        } else {
            camera_commands.remove<DeferredPrepass>();
        }
    }
    const auto remove_dead_views = [&live_views](const auto& entry) { return !live_views->contains(entry.first); };
    std::erase_if(opaque_prepass_phases->phases, remove_dead_views);
    std::erase_if(alpha_mask_prepass_phases->phases, remove_dead_views);
    std::erase_if(opaque_deferred_phases->phases, remove_dead_views);
    std::erase_if(alpha_mask_deferred_phases->phases, remove_dead_views);
}

void epix::core_graph::core_3d::prepare_prepass_textures(
    Commands cmd,
    ResMut<::epix::render::render_resource::TextureCache> texture_cache,
    Res<wgpu::Device> device,
    Res<phase::ViewBinnedRenderPhases<Opaque3dPrepass>> opaque_prepass_phases,
    Res<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>> alpha_mask_prepass_phases,
    Res<phase::ViewBinnedRenderPhases<Opaque3dDeferred>> opaque_deferred_phases,
    Res<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>> alpha_mask_deferred_phases,
    Query<Item<Entity,
               const ::epix::render::camera::ExtractedCamera&,
               const ::epix::render::view::ExtractedView&,
               const ::epix::render::view::Msaa&,
               Has<DepthPrepass>,
               Has<NormalPrepass>,
               Has<MotionVectorPrepass>,
               Has<DeferredPrepass>>>
        views) {
    // Bevy `prepare_prepass_textures` (core_3d/mod.rs:991-1223) caches one
    // texture per output target, so cameras compositing onto the same target
    // share it: `.entry(camera.target.clone())` at line 1049 and the sibling
    // caches.
    //
    // Each prepass texture clears to black on its first use per frame, which is
    // what Bevy passes as `ColorAttachment::new(t, None, None, Some(BLACK))`.
    constexpr glm::vec4 kPrepassClear{0.0f, 0.0f, 0.0f, 1.0f};

    std::unordered_map<std::optional<::epix::camera::RenderTargetId>,
                       ::epix::render::render_resource::CachedTexture,
                       decltype(&render_target_hash)>
        depth_textures{0, &render_target_hash};
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>,
                       ::epix::render::render_resource::CachedTexture,
                       decltype(&render_target_hash)>
        normal_textures{0, &render_target_hash};
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>,
                       ::epix::render::render_resource::CachedTexture,
                       decltype(&render_target_hash)>
        motion_vector_textures{0, &render_target_hash};
    // C15: the deferred G-buffer and lighting-pass id textures, also one per
    // output target.
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>,
                       ::epix::render::render_resource::CachedTexture,
                       decltype(&render_target_hash)>
        deferred_textures{0, &render_target_hash};
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>,
                       ::epix::render::render_resource::CachedTexture,
                       decltype(&render_target_hash)>
        deferred_lighting_id_textures{0, &render_target_hash};

    for (auto&& [entity, camera, extracted_view, msaa, depth_prepass, normal_prepass, motion_vector_prepass,
                 deferred_prepass] : views.iter()) {
        static_cast<void>(deferred_prepass);
        const auto& retained_view = extracted_view.retained_view_entity;
        // Bevy removes the component when the view is in no prepass *or*
        // deferred phase at all (`core_3d/mod.rs:1033-1040`); this membership
        // test is the equivalent.
        if (!opaque_prepass_phases->phases.contains(retained_view) &&
            !alpha_mask_prepass_phases->phases.contains(retained_view) &&
            !opaque_deferred_phases->phases.contains(retained_view) &&
            !alpha_mask_deferred_phases->phases.contains(retained_view)) {
            cmd.entity(entity).remove<ViewPrepassTextures>();
            continue;
        }
        const auto physical_target_size = camera.physical_target_size;
        if (!physical_target_size) continue;

        const auto size         = wgpu::Extent3D{physical_target_size->x, physical_target_size->y, 1};
        const auto sample_count = ::epix::render::view::samples(msaa);
        const auto target       = render_target_of(camera);

        const auto cached_for = [&](auto& textures,
                                    std::string_view label,
                                    wgpu::TextureFormat format,
                                    wgpu::TextureUsage usage,
                                    std::uint32_t texture_samples) {
            const auto existing = textures.find(target);
            if (existing != textures.end()) return existing->second;
            wgpu::TextureDescriptor descriptor;
            descriptor.setLabel(label.data())
                .setSize(size)
                .setMipLevelCount(1)
                .setSampleCount(texture_samples)
                .setDimension(wgpu::TextureDimension::e2D)
                .setFormat(format)
                .setUsage(usage);
            const ::epix::render::render_resource::TextureCacheKey texture_key{
                .format                = format,
                .dimension             = wgpu::TextureDimension::e2D,
                .width                 = size.width,
                .height                = size.height,
                .depth_or_array_layers = 1,
                .mip_level_count       = 1,
                .sample_count          = texture_samples,
                .usage                 = usage,
                .label                 = std::string(label),
            };
            return textures.emplace(target, texture_cache->get(*device, texture_key, descriptor)).first->second;
        };

        ViewPrepassTextures textures{};
        textures.size = size;

        if (depth_prepass) {
            // Bevy's prepass depth texture is a copy destination (the prepass
            // node copies the view depth into it), a render attachment, and
            // sampleable.
            const auto cached = cached_for(depth_textures, "prepass_depth_texture", CORE_3D_DEPTH_FORMAT,
                                           static_cast<wgpu::TextureUsage>(wgpu::TextureUsage::eCopyDst |
                                                                           wgpu::TextureUsage::eRenderAttachment |
                                                                           wgpu::TextureUsage::eTextureBinding), sample_count);
            textures.depth =
                ::epix::render::render_resource::ColorAttachment{cached, std::nullopt, std::nullopt, kPrepassClear};
        }
        if (normal_prepass) {
            const auto cached = cached_for(normal_textures, "prepass_normal_texture", NORMAL_PREPASS_FORMAT,
                                           static_cast<wgpu::TextureUsage>(wgpu::TextureUsage::eRenderAttachment |
                                                                           wgpu::TextureUsage::eTextureBinding), sample_count);
            textures.normal =
                ::epix::render::render_resource::ColorAttachment{cached, std::nullopt, std::nullopt, kPrepassClear};
        }
        if (motion_vector_prepass) {
            const auto cached = cached_for(motion_vector_textures, "prepass_motion_vectors_texture",
                                           MOTION_VECTOR_PREPASS_FORMAT,
                                           static_cast<wgpu::TextureUsage>(wgpu::TextureUsage::eRenderAttachment |
                                                                           wgpu::TextureUsage::eTextureBinding), sample_count);
            textures.motion_vectors =
                ::epix::render::render_resource::ColorAttachment{cached, std::nullopt, std::nullopt, kPrepassClear};
        }
        if (deferred_prepass) {
            // The G-buffer and the lighting-pass id are both sampleable render
            // attachments, created single-sampled even for an MSAA view
            // (Bevy `sample_count: 1` in both descriptors).
            const auto cached = cached_for(deferred_textures, "prepass_deferred_texture", DEFERRED_PREPASS_FORMAT,
                                           static_cast<wgpu::TextureUsage>(wgpu::TextureUsage::eRenderAttachment |
                                                                           wgpu::TextureUsage::eTextureBinding),
                                           kSingleSample);
            textures.deferred =
                ::epix::render::render_resource::ColorAttachment{cached, std::nullopt, std::nullopt, kPrepassClear};

            const auto lighting_id =
                cached_for(deferred_lighting_id_textures, "deferred_lighting_pass_id_texture",
                           DEFERRED_LIGHTING_PASS_ID_FORMAT,
                           static_cast<wgpu::TextureUsage>(wgpu::TextureUsage::eRenderAttachment |
                                                           wgpu::TextureUsage::eTextureBinding),
                           kSingleSample);
            textures.deferred_lighting_pass_id =
                ::epix::render::render_resource::ColorAttachment{lighting_id, std::nullopt, std::nullopt, kPrepassClear};
        }

        cmd.entity(entity).insert(std::move(textures));
    }
}

// C15 deferred G-buffer. Bevy's `deferred/node.rs` shares one body between the
// early and late nodes and draws the two binned deferred phases; the queuing
// half comes from the material plugin in the `pbr` module, so these bins are
// empty until that lands.

namespace {
/** @brief Shared body of Bevy's `run_deferred_prepass` (`deferred/node.rs:108`). */
std::expected<void, graph::NodeRunError> run_deferred_prepass(
    graph::GraphContext& graph,
    graph::RenderContext& render_context,
    const ::epix::render::camera::ExtractedCamera& camera,
    const ::epix::render::view::ExtractedView& extracted_view,
    const ::epix::render::view::ViewDepthTexture& view_depth_texture,
    const ViewPrepassTextures& view_prepass_textures,
    bool is_late,
    std::optional<glm::uvec2> resolution_override,
    const World& world,
    std::string_view label) {
    // Bevy only reads `is_late` inside its WebGL-only fallback, so on native the
    // parameter survives but changes nothing (deferred/node.rs:155-193).
    static_cast<void>(is_late);
    const auto opaque_deferred_phases = world.get_resource<phase::ViewBinnedRenderPhases<Opaque3dDeferred>>();
    const auto alpha_mask_deferred_phases = world.get_resource<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>>();
    if (!opaque_deferred_phases || !alpha_mask_deferred_phases) return {};

    const auto opaque_phase = opaque_deferred_phases->get().phases.find(extracted_view.retained_view_entity);
    const auto alpha_mask_phase =
        alpha_mask_deferred_phases->get().phases.find(extracted_view.retained_view_entity);
    if (opaque_phase == opaque_deferred_phases->get().phases.end() ||
        alpha_mask_phase == alpha_mask_deferred_phases->get().phases.end()) {
        return {};
    }

    // Bevy's attachment order: normals, motion vectors, the deferred G-buffer,
    // then the deferred lighting-pass id.
    std::vector<wgpu::RenderPassColorAttachment> color_attachments;
    color_attachments.push_back(view_prepass_textures.normal
                                    ? view_prepass_textures.normal->get_attachment()
                                    : wgpu::RenderPassColorAttachment{});
    color_attachments.push_back(view_prepass_textures.motion_vectors
                                    ? view_prepass_textures.motion_vectors->get_attachment()
                                    : wgpu::RenderPassColorAttachment{});
    if (view_prepass_textures.deferred) {
        // Bevy builds this attachment from `package_double_buffered_texture`,
        // which always passes `Some(LinearRgba::BLACK)` as the clear color
        // (`core_3d/mod.rs:1231-1236`), so `get_attachment()` clears on both the
        // early and the late node. Only the WebGL fallback swaps in `Load`, and
        // that path is compiled out natively (`deferred/node.rs:170-191`).
        color_attachments.push_back(view_prepass_textures.deferred->get_attachment());
    } else {
        color_attachments.push_back(wgpu::RenderPassColorAttachment{});
    }
    color_attachments.push_back(view_prepass_textures.deferred_lighting_pass_id
                                    ? view_prepass_textures.deferred_lighting_pass_id->get_attachment()
                                    : wgpu::RenderPassColorAttachment{});

    // Bevy empties the color-attachment list when every slot is absent, so the
    // pass needs no fragment shader at all (deferred/node.rs:202-205). The same
    // rule `run_prepass` applies for the non-deferred pass.
    const bool has_any_color = view_prepass_textures.normal.has_value() ||
                               view_prepass_textures.motion_vectors.has_value() ||
                               view_prepass_textures.deferred.has_value() ||
                               view_prepass_textures.deferred_lighting_pass_id.has_value();

    const auto depth_attachment = view_depth_texture.get_attachment(wgpu::StoreOp::eStore);
    // Bevy `Viewport::from_viewport_and_override(camera.viewport.as_ref(),
    // resolution_override)` (`deferred/node.rs:229-233`).
    const auto viewport    = ::epix::camera::Viewport::from_viewport_and_override(camera.viewport, resolution_override);
    const auto view_entity = graph.view_entity();
    const auto* world_ptr       = std::addressof(world);
    const auto* opaque_ptr      = std::addressof(opaque_phase->second);
    const auto* mask_ptr        = std::addressof(alpha_mask_phase->second);
    // Bevy copies the view depth texture into the prepass depth texture after the
    // deferred pass, exactly as the non-deferred prepass node does
    // (deferred/node.rs:260-267). Without it a deferred view's
    // `ViewPrepassTextures::depth` is never written, so every consumer of the
    // prepass depth reads uninitialised content.
    const auto has_prepass_depth = view_prepass_textures.depth.has_value();
    const auto prepass_depth_texture =
        has_prepass_depth ? std::optional<wgpu::Texture>{view_prepass_textures.depth->texture.texture} : std::nullopt;
    const auto prepass_copy_size  = view_prepass_textures.size;
    const auto depth_texture_copy = view_depth_texture.texture;

    render_context.add_command_buffer_generation_task(
        [color_attachments, has_any_color, depth_attachment, viewport, view_entity, world_ptr, opaque_ptr, mask_ptr,
         has_prepass_depth, prepass_depth_texture, prepass_copy_size, depth_texture_copy, label](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(
                wgpu::CommandEncoderDescriptor().setLabel("deferred_prepass_command_encoder"));
            {
                wgpu::RenderPassDescriptor descriptor;
                descriptor.setLabel(label.data());
                if (has_any_color) descriptor.setColorAttachments(color_attachments);
                descriptor.setDepthStencilAttachment(depth_attachment);
                auto render_pass = encoder.beginRenderPass(descriptor);
                if (viewport) {
                    const auto& vp = *viewport;
                    render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                            static_cast<float>(vp.physical_position.y),
                                            static_cast<float>(vp.physical_size.x),
                                            static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
                }
                if (!opaque_ptr->is_empty()) opaque_ptr->render(render_pass, *world_ptr, view_entity);
                if (!mask_ptr->is_empty()) mask_ptr->render(render_pass, *world_ptr, view_entity);
                render_pass.end();
            }

            if (has_prepass_depth && prepass_depth_texture) {
                auto source          = wgpu::TexelCopyTextureInfo();
                source.texture       = depth_texture_copy;
                source.mipLevel      = 0;
                source.origin        = wgpu::Origin3D{0, 0, 0};
                source.aspect        = wgpu::TextureAspect::eAll;
                auto destination     = wgpu::TexelCopyTextureInfo();
                destination.texture  = *prepass_depth_texture;
                destination.mipLevel = 0;
                destination.origin   = wgpu::Origin3D{0, 0, 0};
                destination.aspect   = wgpu::TextureAspect::eAll;
                encoder.copyTextureToTexture(source, destination, prepass_copy_size);
            }

            return encoder.finish();
        });
    return {};
}
}  // namespace

std::expected<void, graph::NodeRunError> EarlyDeferredGBufferPrepassNode::run(
    graph::GraphContext& graph,
    graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const World& world) const {
    auto&& [camera, extracted_view, depth_texture, prepass_textures, resolution_override, occlusion_culling,
            no_indirect_drawing] = view;
    static_cast<void>(occlusion_culling);
    static_cast<void>(no_indirect_drawing);
    return run_deferred_prepass(graph, render_context, camera, extracted_view, depth_texture, prepass_textures, false,
                                resolution_override.transform([](const auto& value) { return value.get().size; }), world,
                                "early deferred gbuffer prepass");
}

std::expected<void, graph::NodeRunError> LateDeferredGBufferPrepassNode::run(
    graph::GraphContext& graph,
    graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const World& world) const {
    // Bevy only needs a late deferred prepass when occlusion culling is active
    // and the view allows indirect drawing (deferred/node.rs:83-86); the same
    // missing bin split that keeps `LatePrepassNode` a no-op applies here, so the
    // early deferred node owns the whole pass (C15-D2).
    static_cast<void>(graph);
    static_cast<void>(render_context);
    static_cast<void>(view);
    static_cast<void>(world);
    return {};
}

void Core3dGraph::add_to(graph::RenderGraph& g, World& world) {
    spdlog::debug("[render.core_graph] Adding Core3D sub-graph to render graph.");
    graph::RenderGraph g3d;
    // Every label in Bevy's default chain is now a real node: C14 installed the
    // two prepass nodes, C15 the two deferred G-buffer nodes, and C15-D4 the
    // lighting-id copy.
    g3d.add_node(Node3d::EarlyPrepass, graph::ViewNodeRunner{EarlyPrepassNode{}, world});
    g3d.add_node(Node3d::LatePrepass, graph::ViewNodeRunner{LatePrepassNode{}, world});
    g3d.add_node(Node3d::EarlyDeferredPrepass, graph::ViewNodeRunner{EarlyDeferredGBufferPrepassNode{}, world});
    g3d.add_node(Node3d::LateDeferredPrepass, graph::ViewNodeRunner{LateDeferredGBufferPrepassNode{}, world});
    g3d.add_node(Node3d::CopyDeferredLightingId, graph::ViewNodeRunner{CopyDeferredLightingIdNode{}, world});
    g3d.add_node(Node3d::EndPrepasses, graph::EmptyNode{});
    g3d.add_node(Node3d::StartMainPass, graph::EmptyNode{});
    g3d.add_node(Node3d::MainOpaquePass, graph::ViewNodeRunner{MainOpaquePass3dNode{}, world});
    g3d.add_node(Node3d::MainTransmissivePass, graph::ViewNodeRunner{MainTransmissivePass3dNode{}, world});
    g3d.add_node(Node3d::MainTransparentPass, graph::ViewNodeRunner{MainTransparentPass3dNode{}, world});
    g3d.add_node(Node3d::EndMainPass, graph::EmptyNode{});
    g3d.add_node(Node3d::StartMainPassPostProcessing, graph::EmptyNode{});
    g3d.add_node(Node3d::Tonemapping, graph::ViewNodeRunner{TonemappingNode{}, world});
    g3d.add_node(Node3d::EndMainPassPostProcessing, graph::EmptyNode{});
    g3d.add_node(Node3d::Upscaling, graph::ViewNodeRunner{UpscalingNode{}, world});
    g3d.add_node_edges(Node3d::EarlyPrepass, Node3d::EarlyDeferredPrepass, Node3d::LatePrepass,
                       Node3d::LateDeferredPrepass, Node3d::CopyDeferredLightingId, Node3d::EndPrepasses,
                       Node3d::StartMainPass, Node3d::MainOpaquePass, Node3d::MainTransmissivePass,
                       Node3d::MainTransparentPass, Node3d::EndMainPass,
                       Node3d::StartMainPassPostProcessing, Node3d::Tonemapping,
                       Node3d::EndMainPassPostProcessing, Node3d::Upscaling);
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
    // Bevy Core3dPlugin installs the skybox plugin (core_3d/mod.rs:150).
    app.add_plugins(SkyboxPlugin{});

    // The motion-vector prepass's previous-frame view data: the main world stores
    // the camera's current matrices (`update_previous_view_data`), the extraction
    // carries them to the render world one frame late, and the prepare system
    // stamps each view with its buffer offset.
    app.add_plugins(render::ExtractComponentPlugin<PreviousViewData>{});
    app.add_systems(epix::app::Update, into(update_previous_view_data).set_name("update previous view data"));
    if (auto render_sub_app = app.get_sub_app_mut(render::Render)) {
        render_sub_app->get().add_systems(render::RenderStartup,
                                          into([](Commands commands, Res<wgpu::Device> device) {
                                              // A dynamic uniform buffer needs the device's
                                              // alignment, which a default-constructed one
                                              // does not have.
                                              wgpu::Limits limits{};
                                              device->getLimits(&limits);
                                              commands.insert_resource(PreviousViewUniforms{
                                                  .uniforms = render::render_resource::
                                                      DynamicUniformBuffer<PreviousViewData>::new_with_alignment(
                                                          limits.minUniformBufferOffsetAlignment)});
                                          }).set_name("init previous view uniforms"));
        render_sub_app->get().add_systems(render::Render,
                                          into(prepare_previous_view_uniforms)
                                              .in_set(render::RenderSystems::Prepare)
                                              .set_name("prepare previous view uniforms"));
    }
    // Bevy registers this in the main world's PostUpdate (core_3d/mod.rs:151).
    app.add_systems(::epix::app::PostUpdate, into(check_msaa).set_name("check msaa"));
    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().init_resource<phase::DrawFunctions<Opaque3d>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<AlphaMask3d>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<Transmissive3d>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<Transparent3d>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<Opaque3d>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<AlphaMask3d>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transmissive3d>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transparent3d>>();
        // Bevy Core3dPlugin also initializes the prepass draw functions and
        // binned phases (core_3d/mod.rs:161-168).
        render_app.world_mut().init_resource<phase::DrawFunctions<Opaque3dPrepass>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<AlphaMask3dPrepass>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>>();
        // C15: the same pair for the deferred G-buffer phases.
        render_app.world_mut().init_resource<phase::DrawFunctions<Opaque3dDeferred>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<AlphaMask3dDeferred>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<Opaque3dDeferred>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>>();
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
        // Bevy orders this after `prepare_view_targets` inside
        // `RenderSystems::ManageViews` (core_3d/mod.rs:180-182); Epix has no
        // separate view-target system, and ManageViews already precedes the
        // `PrepareResources` system that reads `depth_texture_usages`.
        render_app.add_systems(Render, into(configure_occlusion_culling_view_targets)
                                           .in_set(RenderSystems::ManageViews)
                                           .set_name("configure occlusion culling view targets"));
        render_app.add_systems(Render, into(prepare_core_3d_transmission_textures)
                                           .in_set(RenderSystems::PrepareResources)
                                           .set_name("prepare core 3d transmission textures"));
        render_app.add_systems(ExtractSchedule,
                               into(extract_core_3d_camera_phases).set_name("extract core 3d camera phases"));
        // Bevy registers this single system for both the prepass and the
        // deferred phases (core_3d/mod.rs:173-187); there is no separate
        // deferred extraction.
        render_app.add_systems(ExtractSchedule,
                               into(extract_camera_prepass_phase).set_name("extract camera prepass phase"));
        render_app.add_systems(Render, into(prepare_prepass_textures)
                                           .in_set(RenderSystems::PrepareResources)
                                           .set_name("prepare prepass textures"));
        return std::make_optional(std::ref(render_app));
    });
}
