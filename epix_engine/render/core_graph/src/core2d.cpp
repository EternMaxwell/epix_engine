
#include <spdlog/spdlog.h>

#include <epix/core_graph.hpp>
#include <array>
#include <optional>
#include <unordered_set>
using namespace epix::render;
using namespace epix::core_graph::core_2d;
using namespace epix::ecs;
using namespace epix::app;

void epix::core_graph::core_2d::extract_core_2d_camera_phases(
    ResMut<phase::ViewSortedRenderPhases<Transparent2D>> transparent_phases,
    ResMut<phase::ViewBinnedRenderPhases<Opaque2D>> opaque_phases,
    ResMut<phase::ViewBinnedRenderPhases<AlphaMask2D>> alpha_mask_phases,
    Extract<Query<Item<Entity, const ::epix::camera::Camera&>, With<::epix::camera::Camera2d>>> cameras,
    Local<std::unordered_set<::epix::render::view::RetainedViewEntity>> live_views) {
    live_views->clear();
    for (auto&& [entity, camera] : cameras.iter()) {
        if (!camera.is_active) continue;
        const auto retained_view = ::epix::render::view::RetainedViewEntity::create(
            ::epix::render::sync_world::MainEntity{entity}, std::nullopt, 0);
        transparent_phases->insert_or_clear(retained_view);
        opaque_phases->prepare_for_new_frame(retained_view, ::epix::render::batching::GpuPreprocessingMode::None);
        alpha_mask_phases->prepare_for_new_frame(retained_view,
                                                 ::epix::render::batching::GpuPreprocessingMode::None);
        live_views->insert(retained_view);
    }
    const auto remove_dead_views = [&live_views](const auto& entry) { return !live_views->contains(entry.first); };
    std::erase_if(*transparent_phases, remove_dead_views);
    std::erase_if(opaque_phases->phases, remove_dead_views);
    std::erase_if(alpha_mask_phases->phases, remove_dead_views);
}

std::expected<void, graph::NodeRunError> MainOpaquePass2DNode::run(
    graph::GraphContext& graph,
    graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const World& world) const {
    auto&& [camera, extracted_view, target, depth] = view;
    const auto opaque_phases = world.get_resource<phase::ViewBinnedRenderPhases<Opaque2D>>();
    const auto alpha_mask_phases = world.get_resource<phase::ViewBinnedRenderPhases<AlphaMask2D>>();
    if (!opaque_phases || !alpha_mask_phases) return {};

    const auto opaque_phase = opaque_phases->get().phases.find(extracted_view.retained_view_entity);
    const auto alpha_mask_phase = alpha_mask_phases->get().phases.find(extracted_view.retained_view_entity);
    if (opaque_phase == opaque_phases->get().phases.end() || alpha_mask_phase == alpha_mask_phases->get().phases.end()) {
        return {};
    }

    const auto color_attachments = std::array{target.get_color_attachment()};
    const auto depth_attachment = depth.get_attachment(wgpu::StoreOp::eStore);
    const auto viewport = camera.viewport;
    const auto view_entity = graph.view_entity();
    const auto* world_ptr = std::addressof(world);
    const auto* opaque_phase_ptr = std::addressof(opaque_phase->second);
    const auto* alpha_mask_phase_ptr = std::addressof(alpha_mask_phase->second);
    render_context.add_command_buffer_generation_task(
        [color_attachments, depth_attachment, viewport, view_entity, world_ptr, opaque_phase_ptr,
         alpha_mask_phase_ptr](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("main_opaque_pass_2d"));
            auto render_pass = encoder.beginRenderPass(
                wgpu::RenderPassDescriptor()
                    .setLabel("main_opaque_pass_2d")
                    .setColorAttachments(color_attachments)
                    .setDepthStencilAttachment(depth_attachment));
            if (viewport) {
                const auto& vp = *viewport;
                render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                        static_cast<float>(vp.physical_position.y),
                                        static_cast<float>(vp.physical_size.x),
                                        static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
            }
            if (!opaque_phase_ptr->is_empty()) opaque_phase_ptr->render(render_pass, *world_ptr, view_entity);
            if (!alpha_mask_phase_ptr->is_empty()) alpha_mask_phase_ptr->render(render_pass, *world_ptr, view_entity);
            render_pass.end();
            return encoder.finish();
        });
    return {};
}

std::expected<void, graph::NodeRunError> MainTransparentPass2DNode::run(
    graph::GraphContext& graph,
    graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const World& world) const {
    auto&& [camera, extracted_view, target, depth] = view;
    const auto transparent_phases = world.get_resource<phase::ViewSortedRenderPhases<Transparent2D>>();
    if (!transparent_phases) return {};
    const auto transparent_phase = transparent_phases->get().find(extracted_view.retained_view_entity);
    if (transparent_phase == transparent_phases->get().end()) return {};

    const auto color_attachments = std::array{target.get_color_attachment()};
    const auto depth_attachment = depth.get_attachment(wgpu::StoreOp::eStore);
    const auto viewport = camera.viewport;
    const auto view_entity = graph.view_entity();
    const auto* world_ptr = std::addressof(world);
    const auto* transparent_phase_ptr = std::addressof(transparent_phase->second);
    render_context.add_command_buffer_generation_task(
        [color_attachments, depth_attachment, viewport, view_entity, world_ptr, transparent_phase_ptr](wgpu::Device device) {
            auto encoder =
                device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("main_transparent_pass_2d"));
            auto render_pass = encoder.beginRenderPass(
                wgpu::RenderPassDescriptor()
                    .setLabel("main_transparent_pass_2d")
                    .setColorAttachments(color_attachments)
                    .setDepthStencilAttachment(depth_attachment));
            if (viewport) {
                const auto& vp = *viewport;
                render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                        static_cast<float>(vp.physical_position.y),
                                        static_cast<float>(vp.physical_size.x),
                                        static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
            }
            if (!transparent_phase_ptr->items.empty()) transparent_phase_ptr->render(render_pass, *world_ptr, view_entity);
            render_pass.end();
            return encoder.finish();
        });
    return {};
}

void Core2dGraph::add_to(graph::RenderGraph& g, World& world) {
    spdlog::debug("[render.core_graph] Adding Core2D sub-graph to render graph.");
    graph::RenderGraph g2d;
    g2d.add_node(Core2dNodes::StartMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::MainTransparentPass, graph::ViewNodeRunner{MainTransparentPass2DNode{}, world});
    g2d.add_node(Core2dNodes::MainOpaquePass, graph::ViewNodeRunner{MainOpaquePass2DNode{}, world});
    g2d.add_node(Core2dNodes::EndMainPass, graph::EmptyNode{});
    // These endpoints are intentionally installed by Core2dPlugin: Bevy does
    // the same so optional post-processing plugins can insert work between
    // them. Tonemapping is the C16 GPU node (Bevy TonemappingNode): it skips
    // non-HDR views and the None method, so the default 2D camera is unchanged.
    g2d.add_node(Core2dNodes::StartMainPassPostProcessing, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::Tonemapping, graph::ViewNodeRunner{TonemappingNode{}, world});
    g2d.add_node(Core2dNodes::EndMainPassPostProcessing, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::Upscaling, graph::ViewNodeRunner{UpscalingNode{}, world});
    g2d.add_node_edges(Core2dNodes::StartMainPass, Core2dNodes::MainOpaquePass, Core2dNodes::MainTransparentPass,
                       Core2dNodes::EndMainPass, Core2dNodes::StartMainPassPostProcessing,
                       Core2dNodes::Tonemapping, Core2dNodes::EndMainPassPostProcessing, Core2dNodes::Upscaling);
    g.add_sub_graph(Core2d, std::move(g2d));
}

void epix::core_graph::core_2d::prepare_core_2d_depth_textures(
    Commands cmd,
    ResMut<render::render_resource::TextureCache> texture_cache,
    Res<wgpu::Device> device,
    Res<phase::ViewSortedRenderPhases<Transparent2D>> transparent_phases,
    Res<phase::ViewBinnedRenderPhases<Opaque2D>> opaque_phases,
    Query<Item<Entity,
               const render::camera::ExtractedCamera&,
               const render::view::ExtractedView&,
               const render::view::Msaa&>, With<::epix::camera::Camera2d>> views) {
    // Bevy caches one depth texture per output target per frame, so cameras
    // compositing onto the same target share it (prepare_core_2d_depth_textures).
    const auto target_hash = [](const std::optional<::epix::camera::RenderTargetId>& target) {
        return target ? ::epix::camera::RenderTargetIdHash{}(*target) : std::size_t{0};
    };
    std::unordered_map<std::optional<::epix::camera::RenderTargetId>, render::render_resource::CachedTexture,
                       decltype(target_hash)>
        textures;
    for (auto&& [entity, camera, extracted_view, msaa] : views.iter()) {
        const auto& retained_view = extracted_view.retained_view_entity;
        if (!opaque_phases->phases.contains(retained_view) || !transparent_phases->contains(retained_view)) {
            continue;
        }
        if (!camera.physical_target_size) {
            continue;
        }
        const auto size = *camera.physical_target_size;
        const auto target = camera.target.transform([](const auto& value) { return value.identity(); });
        const auto sample_count = render::view::samples(msaa);
        auto it = textures.find(target);
        if (it == textures.end()) {
            wgpu::TextureDescriptor descriptor;
            descriptor.setLabel("view_depth_texture")
                .setSize({size.x, size.y, 1})
                .setMipLevelCount(1)
                .setSampleCount(sample_count)
                .setDimension(wgpu::TextureDimension::e2D)
                .setFormat(CORE_2D_DEPTH_FORMAT)
                .setUsage(wgpu::TextureUsage::eRenderAttachment);
            const render::render_resource::TextureCacheKey key{
                .format = CORE_2D_DEPTH_FORMAT,
                .dimension = wgpu::TextureDimension::e2D,
                .width = size.x,
                .height = size.y,
                .depth_or_array_layers = 1,
                .mip_level_count = 1,
                .sample_count = sample_count,
                .usage = wgpu::TextureUsage::eRenderAttachment,
                .label = "view_depth_texture",
            };
            it = textures.emplace(target, texture_cache->get(*device, key, descriptor)).first;
        }
        cmd.entity(entity).insert(render::view::ViewDepthTexture::create(it->second, 0.0f));
    }
}

void Core2dPlugin::attach(App& app) {
    // Mirror Core2dPlugin::build: Camera2d gets the Core2D graph plus the
    // component defaults that define a plain 2D camera output path.
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return DebandDither::Disabled; });
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return render::camera::CameraRenderGraph{Core2d}; });
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return Tonemapping::None; });
    app.add_plugins(render::ExtractComponentPlugin<::epix::camera::Camera2d>{});
    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().init_resource<phase::DrawFunctions<Transparent2D>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<Opaque2D>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<AlphaMask2D>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transparent2D>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<Opaque2D>>();
        render_app.world_mut().init_resource<phase::ViewBinnedRenderPhases<AlphaMask2D>>();
        Core2d.add_to(render_app.resource_mut<graph::RenderGraph>(), render_app.world_mut());

        render_app.add_systems(Render, into(phase::sort_phase_system<Transparent2D>)
                                           .in_set(RenderSystems::PhaseSort)
                                           .set_name("sort transparent 2d phase"));
        render_app.add_systems(Render, into(prepare_core_2d_depth_textures)
                                           .in_set(RenderSystems::PrepareResources)
                                           .set_name("prepare core 2d depth textures"));
        render_app.add_systems(ExtractSchedule,
                               into(extract_core_2d_camera_phases).set_name("extract core 2d camera phases"));
        return std::make_optional(std::ref(render_app));
    });
}
