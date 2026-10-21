#include <epix/pbr/prepass.hpp>

#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/render/bind_group_entries.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bind_group_layout_entries.hpp>

#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace epix::pbr {

void update_previous_view_data(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity, const camera::Camera&, const transform::GlobalTransform&>,
               ecs::Or<ecs::With<camera::Camera3d>, ecs::With<ShadowView>>> views) {
    for (auto&& [entity, camera, camera_transform] : views.iter()) {
        const auto& world_from_view = camera_transform.matrix;
        const auto view_from_world = glm::inverse(world_from_view);
        const auto clip_from_view = camera.clip_from_view();
        const auto view_from_clip = glm::inverse(clip_from_view);
        // EntityCommands::insert already tolerates an entity disappearing before
        // deferred application, matching Bevy's try_insert here.
        commands.entity(entity).insert(core_graph::PreviousViewData{
            .view_from_world = view_from_world,
            .clip_from_world = clip_from_view * view_from_world,
            .clip_from_view = clip_from_view,
            .world_from_clip = world_from_view * view_from_clip,
            .view_from_clip = view_from_clip,
        });
    }
}

void update_mesh_previous_global_transforms(
    ecs::Commands commands,
    ecs::Query<const camera::Camera&, ecs::Or<ecs::With<camera::Camera3d>, ecs::With<ShadowView>>> views,
    ecs::Query<ecs::Item<ecs::Entity, const transform::GlobalTransform&>,
               ecs::Filter<ecs::With<mesh::Mesh3d>, ecs::Without<PreviousGlobalTransform>>> new_meshes,
    ecs::Query<ecs::Item<const transform::GlobalTransform&, ecs::Mut<PreviousGlobalTransform>>,
               ecs::With<mesh::Mesh3d>> meshes) {
    bool should_run = false;
    for (const auto& camera : views.iter()) {
        if (camera.is_active) {
            should_run = true;
            break;
        }
    }
    if (!should_run) return;

    for (auto&& [entity, transform] : new_meshes.iter())
        commands.entity(entity).insert(PreviousGlobalTransform{transform.matrix});
    for (auto&& [transform, previous] : meshes.iter()) {
        if (previous.get().matrix != transform.matrix) previous.get_mut().matrix = transform.matrix;
    }
}

void extract_camera_previous_view_data(
    ecs::Commands commands,
    app::Extract<ecs::Query<ecs::Item<const render::sync_world::RenderEntity&, const camera::Camera&,
                                    ecs::Opt<const core_graph::PreviousViewData&>>,
                            ecs::With<camera::Camera3d>>> cameras) {
    for (auto&& [render_entity, camera, previous] : cameras.iter()) {
        auto entity = commands.get_entity(render_entity.id());
        if (!entity) throw std::runtime_error("Camera entity wasn't synced.");
        if (camera.is_active) {
            if (previous) entity->insert(previous->get());
        } else {
            entity->remove<core_graph::PreviousViewData>();
        }
    }
}

void prepare_previous_view_uniforms(
    ecs::Commands commands, ecs::Res<wgpu::Device> device, ecs::Res<wgpu::Queue> queue,
    ecs::ResMut<core_graph::PreviousViewUniforms> previous_view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const render::view::ExtractedView&,
                         ecs::Opt<const core_graph::PreviousViewData&>>,
               ecs::Or<ecs::With<camera::Camera3d>, ecs::With<ShadowView>>> views) {
    const auto view_count = static_cast<std::size_t>(std::ranges::distance(views.iter()));
    auto writer = previous_view_uniforms->uniforms.get_writer(view_count, *device, *queue);
    if (!writer) return;

    for (auto&& [entity, view, previous] : views.iter()) {
        core_graph::PreviousViewData data;
        if (previous) {
            data = previous->get();
        } else {
            const auto& world_from_view = view.world_from_view.matrix;
            const auto view_from_world = glm::inverse(world_from_view);
            const auto view_from_clip = glm::inverse(view.clip_from_view);
            data = core_graph::PreviousViewData{
                .view_from_world = view_from_world,
                .clip_from_world = view.clip_from_view * view_from_world,
                .clip_from_view = view.clip_from_view,
                .world_from_clip = world_from_view * view_from_clip,
                .view_from_clip = view_from_clip,
            };
        }
        commands.entity(entity).insert(core_graph::PreviousViewUniformOffset{writer->write(data)});
    }
}

void init_prepass_pipeline(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                           ecs::Res<MeshPipeline> mesh_pipeline) {
    // Bevy `init_prepass_pipeline` (`prepass/mod.rs:258-331`).
    wgpu::Limits limits{};
    render_device->getLimits(&limits);

    // Bevy's `RenderDevice::get_supported_read_only_binding_type`, as the mesh
    // pipeline's creation spells it in `mesh_shaders.cpp`.
    const auto read_only_binding_type = [&limits](std::uint32_t buffers_per_shader_stage) {
        return limits.maxStorageBuffersPerShaderStage >= buffers_per_shader_stage
                   ? wgpu::BufferBindingType::eStorage
                   : wgpu::BufferBindingType::eUniform;
    };
    const auto visibility_ranges_buffer_binding_type =
        read_only_binding_type(render::view::VISIBILITY_RANGES_STORAGE_BUFFER_COUNT);

    using namespace render::render_resource::binding_types;
    const auto view_layout_motion_vectors = render::BindGroupLayoutDescriptor::create(
        "prepass_view_layout_motion_vectors",
        render::render_resource::DynamicBindGroupLayoutEntries::new_with_indices(
            wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
            // View
            std::pair{0u, uniform_buffer(true, sizeof(render::view::ViewUniform))},
            // Globals
            std::pair{1u, uniform_buffer(false, sizeof(render::GlobalsUniform))},
            // PreviousViewUniforms
            std::pair{2u, uniform_buffer(true, sizeof(core_graph::PreviousViewData))},
            // VisibilityRanges
            std::pair{14u, buffer_layout(visibility_ranges_buffer_binding_type, false, sizeof(glm::vec4))
                               .visibility(wgpu::ShaderStage::eVertex)})
            .entries());
    const auto view_layout_no_motion_vectors = render::BindGroupLayoutDescriptor::create(
        "prepass_view_layout_no_motion_vectors",
        render::render_resource::DynamicBindGroupLayoutEntries::new_with_indices(
            wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
            // View
            std::pair{0u, uniform_buffer(true, sizeof(render::view::ViewUniform))},
            // Globals
            std::pair{1u, uniform_buffer(false, sizeof(render::GlobalsUniform))},
            // VisibilityRanges
            std::pair{14u, buffer_layout(visibility_ranges_buffer_binding_type, false, sizeof(glm::vec4))
                               .visibility(wgpu::ShaderStage::eVertex)})
            .entries());

    // Bevy reads these three off the device and the adapter; the mesh pipeline
    // read the same facts when it was built, so they come from there.
    const bool depth_clip_control_supported = render_device->hasFeature(wgpu::FeatureName::eDepthClipControl);

    commands.insert_resource(PrepassPipeline{
        .view_layout_motion_vectors    = view_layout_motion_vectors,
        .view_layout_no_motion_vectors = view_layout_no_motion_vectors,
        .mesh_layouts                  = mesh_pipeline->mesh_layouts,
        .empty_layout                  = render::BindGroupLayoutDescriptor::create(
            "prepass_empty_layout", std::vector<wgpu::BindGroupLayoutEntry>{}),
        .skins_use_uniform_buffers    = mesh_pipeline->skins_use_uniform_buffers,
        .depth_clip_control_supported = depth_clip_control_supported,
        .binding_arrays_are_usable    = mesh_pipeline->binding_arrays_are_usable,
    });
}

void init_prepass_view_bind_group(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                                  ecs::Res<render::PipelineServer> pipeline_server,
                                  ecs::Res<PrepassPipeline> pipeline) {
    // Bevy `init_prepass_view_bind_group` (`prepass/mod.rs:692-708`).
    commands.insert_resource(PrepassViewBindGroup{
        .motion_vectors    = std::nullopt,
        .no_motion_vectors = std::nullopt,
        .empty_bind_group  = render_device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("prepass_view_empty_bind_group")
                .setLayout(pipeline_server->get_bind_group_layout(pipeline->empty_layout))
                .setEntries(std::vector<wgpu::BindGroupEntry>{})),
    });
}

void prepare_prepass_view_bind_group(
    ecs::Res<wgpu::Device> render_device, ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<PrepassPipeline> prepass_pipeline, ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<render::GlobalsBuffer> globals_buffer,
    ecs::Res<core_graph::PreviousViewUniforms> previous_view_uniforms,
    ecs::Res<render::view::RenderVisibilityRanges> visibility_ranges,
    ecs::ResMut<PrepassViewBindGroup> prepass_view_bind_group) {
    // Bevy `prepare_prepass_view_bind_group` (`prepass/mod.rs:710-760`).
    using render::render_resource::BindGroupEntries;

    const auto view_binding            = view_uniforms->uniforms.binding();
    const auto globals_binding         = globals_buffer->buffer.binding();
    const auto* visibility_ranges_data = visibility_ranges->buffer_ref().buffer();
    if (!view_binding || !globals_binding || visibility_ranges_data == nullptr) return;

    prepass_view_bind_group->no_motion_vectors = render_device->createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("prepass_view_no_motion_vectors_bind_group")
            .setLayout(pipeline_server->get_bind_group_layout(prepass_pipeline->view_layout_no_motion_vectors))
            .setEntries(BindGroupEntries<>::with_indices(
                            std::pair{0u, BindGroupEntries<>::buffer_binding(view_binding->get(), 0,
                                                                            sizeof(render::view::ViewUniform))},
                            std::pair{1u, BindGroupEntries<>::buffer_binding(globals_binding->get(), 0,
                                                                            sizeof(render::GlobalsUniform))},
                            std::pair{14u, BindGroupEntries<>::buffer_binding(*visibility_ranges_data, 0,
                                                                             sizeof(glm::vec4))})
                            .entries()));

    const auto previous_view_binding = previous_view_uniforms->uniforms.binding();
    if (!previous_view_binding) return;

    prepass_view_bind_group->motion_vectors = render_device->createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("prepass_view_motion_vectors_bind_group")
            .setLayout(pipeline_server->get_bind_group_layout(prepass_pipeline->view_layout_motion_vectors))
            .setEntries(BindGroupEntries<>::with_indices(
                            std::pair{0u, BindGroupEntries<>::buffer_binding(view_binding->get(), 0,
                                                                            sizeof(render::view::ViewUniform))},
                            std::pair{1u, BindGroupEntries<>::buffer_binding(globals_binding->get(), 0,
                                                                            sizeof(render::GlobalsUniform))},
                            std::pair{2u, BindGroupEntries<>::buffer_binding(*previous_view_binding, 0,
                                                                            sizeof(core_graph::PreviousViewData))},
                            std::pair{14u, BindGroupEntries<>::buffer_binding(*visibility_ranges_data, 0,
                                                                             sizeof(glm::vec4))})
                            .entries()));
}

void PrepassPipelinePlugin::attach(epix::app::App& app) {
    // Bevy `PrepassPipelinePlugin::build` (`prepass/mod.rs:71-99`).
    auto render_app = app.get_sub_app_mut(epix::render::Render);
    if (!render_app) return;
    auto& render = render_app->get();

    // Bevy chains the first two, and orders the first after `init_material_pipeline`
    // (`prepass/mod.rs:86-89`), which `MaterialsPlugin` registers in `RenderStartup`.
    render.add_systems(epix::render::RenderStartup, epix::ecs::into(init_prepass_pipeline)
                                                         .after(init_material_pipeline)
                                                         .set_name("init prepass pipeline"));
    render.add_systems(epix::render::RenderStartup, epix::ecs::into(init_prepass_view_bind_group)
                                                         .after(init_prepass_pipeline)
                                                         .set_name("init prepass view bind group"));
    render.add_systems(epix::render::Render, epix::ecs::into(prepare_prepass_view_bind_group)
                                                 .in_set(epix::render::RenderSystems::PrepareBindGroups)
                                                 .set_name("prepare prepass view bind group"));
}

void PrepassPlugin::attach(epix::app::App& app) {
    // Epix's plugin registry deduplicates by type, providing Bevy's
    // AnyPrepassPluginLoaded guard without another public resource.
    app.add_systems(app::PreUpdate, ecs::into(update_mesh_previous_global_transforms, update_previous_view_data)
                                       .set_names(std::array{"update mesh previous global transforms",
                                                             "update previous view data"}));
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) return;
    auto& render = render_app->get();
    render.add_systems(render::ExtractSchedule, ecs::into(extract_camera_previous_view_data)
                                                   .set_name("extract camera previous view data"));
    render.add_systems(render::Render, ecs::into(prepare_previous_view_uniforms)
                                          .in_set(render::RenderSystems::PrepareResources)
                                          .set_name("prepare previous view uniforms"));

    // Bevy's four `add_render_command::<P, DrawPrepass>()` calls
    // (`prepass/mod.rs:158-161`).
    epix::render::phase::app_add_render_command<DrawPrepass<epix::core_graph::core_3d::Opaque3dPrepass>>(render);
    epix::render::phase::app_add_render_command<DrawPrepass<epix::core_graph::core_3d::AlphaMask3dPrepass>>(render);
    epix::render::phase::app_add_render_command<DrawPrepass<epix::core_graph::core_3d::Opaque3dDeferred>>(render);
    epix::render::phase::app_add_render_command<DrawPrepass<epix::core_graph::core_3d::AlphaMask3dDeferred>>(render);
}

}  // namespace epix::pbr
