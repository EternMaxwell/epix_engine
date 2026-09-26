#include <epix/sprite_render/color_material.hpp>
#include <epix/sprite_render/material2d.hpp>
#include <epix/sprite_render/tilemap_chunk.hpp>
#include <string>
#include <utility>

namespace {
/** @brief Resolve a material's optional shader path through the asset server -
 *  Bevy's `ShaderRef::Path` case (`material.rs:491-500`). */
std::optional<epix::assets::Handle<epix::shader::Shader>> load_material_shader(
    std::optional<std::string_view> path, const epix::assets::AssetServer& asset_server) {
    if (!path) return std::nullopt;
    return asset_server.load<epix::shader::Shader>(std::string(*path));
}
}  // namespace

template <typename M>
std::expected<epix::render::RenderPipelineDescriptor, epix::render::SpecializedMeshPipelineError>
epix::sprite_render::Material2dPipeline<M>::specialize(const Material2dKey<M>& key,
                                                       const mesh::MeshVertexBufferLayoutRef& layout) const {
    // Bevy `SpecializedMeshPipeline for Material2dPipeline<M>`
    // (material.rs:446-477): specialize the mesh2d pipeline, then tell the
    // shader which slot the material occupies, apply the material's shader
    // overrides and lay the bind groups out as [view, mesh, material].
    auto descriptor = mesh2d_pipeline.specialize(key.mesh_key, layout);
    if (!descriptor) return std::unexpected(epix::render::SpecializedMeshPipelineError{descriptor.error()});

    const auto material_bind_group =
        epix::shader::ShaderDefVal::from_uint("MATERIAL_BIND_GROUP", MATERIAL_2D_BIND_GROUP_INDEX);
    descriptor->vertex.shader_defs.push_back(material_bind_group);
    if (descriptor->fragment) descriptor->fragment->shader_defs.push_back(material_bind_group);

    if (vertex_shader) descriptor->vertex.shader = *vertex_shader;
    if (fragment_shader && descriptor->fragment) descriptor->fragment->shader = *fragment_shader;

    // The mesh2d specialization produced [view, mesh]; the material is group
    // MATERIAL_2D_BIND_GROUP_INDEX, which is that list plus this descriptor.
    descriptor->layouts.push_back(material2d_layout);

    M::specialize(*descriptor, layout, key);
    return *descriptor;
}

template <typename M>
void epix::sprite_render::specialize_material2d_meshes(
    epix::ecs::Res<Material2dPipeline<M>> material2d_pipeline,
    epix::ecs::ResMut<epix::render::PipelineServer> pipeline_server,
    epix::ecs::ResMut<epix::render::SpecializedMeshPipelines<Material2dPipeline<M>>> pipelines,
    epix::ecs::Res<epix::render::RenderAssets<epix::render::mesh::RenderMesh>> render_meshes,
    epix::ecs::Res<epix::render::RenderAssets<PreparedMaterial2d<M>>> render_materials,
    epix::ecs::ResMut<RenderMesh2dInstances> render_mesh_instances,
    epix::ecs::Res<RenderMaterial2dInstances<M>> render_material_instances,
    epix::ecs::Res<epix::render::phase::ViewSortedRenderPhases<epix::core_graph::core_2d::Transparent2D>>
        transparent_render_phases,
    epix::ecs::Res<epix::render::phase::ViewBinnedRenderPhases<epix::core_graph::core_2d::Opaque2D>>
        opaque_render_phases,
    epix::ecs::Res<epix::render::phase::ViewBinnedRenderPhases<epix::core_graph::core_2d::AlphaMask2D>>
        alpha_mask_render_phases,
    epix::ecs::Query<epix::ecs::Item<const epix::render::sync_world::MainEntity&,
                                     const epix::render::view::ExtractedView&,
                                     const epix::render::view::RenderVisibleEntities&>> views,
    epix::ecs::Res<ViewKeyCache> view_key_cache,
    epix::ecs::Res<EntitySpecializationTickPair<M>> entity_specialization_ticks,
    epix::ecs::Res<ViewSpecializationTicks> view_specialization_ticks,
    epix::ecs::SystemChangeTick ticks,
    epix::ecs::ResMut<SpecializedMaterial2dPipelineCache<M>> specialized_material_pipeline_cache) {
    // Bevy `specialize_material2d_meshes` (material.rs:691-787).
    if (render_material_instances->instances.empty()) return;

    for (auto&& [view_entity, view, visible_entities] : views.iter()) {
        if (!transparent_render_phases->contains(view.retained_view_entity) &&
            !opaque_render_phases->phases.contains(view.retained_view_entity) &&
            !alpha_mask_render_phases->phases.contains(view.retained_view_entity)) {
            continue;
        }
        const auto view_key_it = view_key_cache->keys.find(view_entity);
        if (view_key_it == view_key_cache->keys.end()) continue;
        const auto view_tick_it = view_specialization_ticks->ticks.find(view_entity);
        if (view_tick_it == view_specialization_ticks->ticks.end()) continue;

        auto& view_cache = specialized_material_pipeline_cache->map[view_entity];
        for (auto&& [visible_render_entity, visible_main_entity] :
             visible_entities.template get<epix::mesh::Mesh2d>()) {
            const auto material_it = render_material_instances->instances.find(visible_main_entity);
            if (material_it == render_material_instances->instances.end()) continue;
            const auto mesh_instance_it = render_mesh_instances->instances.find(visible_main_entity);
            if (mesh_instance_it == render_mesh_instances->instances.end()) continue;
            const auto entity_tick_it = entity_specialization_ticks->entities.find(visible_main_entity);
            if (entity_tick_it == entity_specialization_ticks->entities.end()) continue;

            const auto cached_it = view_cache.map.find(visible_main_entity);
            const bool needs_specialization =
                cached_it == view_cache.map.end() ||
                view_tick_it->second.newer_than(cached_it->second.first, ticks.this_run()) ||
                entity_tick_it->second.newer_than(cached_it->second.first, ticks.this_run());
            if (!needs_specialization) continue;

            const auto* material = render_materials->try_get(material_it->second);
            if (!material) continue;
            const auto* mesh = render_meshes->try_get(mesh_instance_it->second.mesh_asset_id);
            if (!mesh) continue;

            const auto mesh_key =
                Mesh2dPipelineKey{view_key_it->second.bits() |
                                  Mesh2dPipelineKey::from_primitive_topology(mesh->primitive_topology()).bits() |
                                  material->properties.mesh_pipeline_key_bits.bits()};

            auto pipeline_id = pipelines->specialize(
                *pipeline_server, *material2d_pipeline,
                Material2dKey<M>{.mesh_key = mesh_key, .bind_group_data = material->key}, mesh->layout);
            if (!pipeline_id) {
                // The error variant has no `std::formatter`, so report without it rather
                // than formatting an unformattable type.
                spdlog::error("[mesh2d] Failed to specialize material pipeline.");
                continue;
            }
            view_cache.map.insert_or_assign(visible_main_entity, std::pair{ticks.this_run(), *pipeline_id});
        }
    }
}

template <typename M>
void epix::sprite_render::queue_material2d_meshes(
    epix::ecs::Res<epix::render::RenderAssets<epix::render::mesh::RenderMesh>> render_meshes,
    epix::ecs::Res<epix::render::RenderAssets<PreparedMaterial2d<M>>> render_materials,
    epix::ecs::ResMut<RenderMesh2dInstances> render_mesh_instances,
    epix::ecs::Res<RenderMaterial2dInstances<M>> render_material_instances,
    epix::ecs::ResMut<epix::render::phase::ViewSortedRenderPhases<epix::core_graph::core_2d::Transparent2D>>
        transparent_render_phases,
    epix::ecs::ResMut<epix::render::phase::ViewBinnedRenderPhases<epix::core_graph::core_2d::Opaque2D>>
        opaque_render_phases,
    epix::ecs::ResMut<epix::render::phase::ViewBinnedRenderPhases<epix::core_graph::core_2d::AlphaMask2D>>
        alpha_mask_render_phases,
    epix::ecs::Query<epix::ecs::Item<const epix::render::sync_world::MainEntity&,
                                     const epix::render::view::ExtractedView&,
                                     const epix::render::view::RenderVisibleEntities&>> views,
    epix::ecs::Res<SpecializedMaterial2dPipelineCache<M>> specialized_material_pipeline_cache) {
    // Bevy `queue_material2d_meshes` (material.rs:804-926).
    if (render_material_instances->instances.empty()) return;

    for (auto&& [view_entity, view, visible_entities] : views.iter()) {
        const auto view_cache_it = specialized_material_pipeline_cache->map.find(view_entity);
        if (view_cache_it == specialized_material_pipeline_cache->map.end()) continue;
        const auto& view_cache = view_cache_it->second;

        auto transparent_phase = transparent_render_phases->find(view.retained_view_entity);
        if (transparent_phase == transparent_render_phases->end()) continue;
        auto opaque_phase = opaque_render_phases->phases.find(view.retained_view_entity);
        if (opaque_phase == opaque_render_phases->phases.end()) continue;
        auto alpha_mask_phase = alpha_mask_render_phases->phases.find(view.retained_view_entity);
        if (alpha_mask_phase == alpha_mask_render_phases->phases.end()) continue;

        for (auto&& [visible_render_entity, visible_main_entity] :
             visible_entities.template get<epix::mesh::Mesh2d>()) {
            const auto cached_it = view_cache.map.find(visible_main_entity);
            if (cached_it == view_cache.map.end()) continue;
            const auto current_change_tick = cached_it->second.first;
            const auto pipeline_id         = cached_it->second.second;

            // Skip entities a binned phase already holds up to date.
            if (opaque_phase->second.validate_cached_entity(visible_main_entity, current_change_tick) ||
                alpha_mask_phase->second.validate_cached_entity(visible_main_entity, current_change_tick)) {
                continue;
            }

            const auto material_it = render_material_instances->instances.find(visible_main_entity);
            if (material_it == render_material_instances->instances.end()) continue;
            const auto mesh_instance_it = render_mesh_instances->instances.find(visible_main_entity);
            if (mesh_instance_it == render_mesh_instances->instances.end()) continue;

            const auto* material = render_materials->try_get(material_it->second);
            if (!material) continue;
            const auto* mesh = render_meshes->try_get(mesh_instance_it->second.mesh_asset_id);
            if (!mesh) continue;

            const auto bind_group_id =
                Material2dBindGroupId{mesh_instance_it->second.material_bind_group_id.value = material_it->second};
            const float mesh_z = mesh_instance_it->second.transforms.world_from_local[3][2];
            // 2D meshes do not support multidraw, so the phase type follows
            // automatic_batching directly (Bevy's comment at material.rs:858-867).
            const auto phase_type    = mesh_instance_it->second.automatic_batching
                                           ? epix::render::phase::BinnedRenderPhaseType::BatchableMesh
                                           : epix::render::phase::BinnedRenderPhaseType::UnbatchableMesh;
            const auto batch_set_key = epix::core_graph::core_2d::BatchSetKey2D{.indexed_value = mesh->indexed()};

            const auto& alpha_mode = material->properties.alpha_mode;
            if (std::holds_alternative<AlphaMode2dOpaque>(alpha_mode)) {
                opaque_phase->second.add(
                    batch_set_key,
                    epix::core_graph::core_2d::Opaque2DBinKey{.pipeline_id = pipeline_id,
                                                              .draw_func   = material->properties.draw_function_id,
                                                              // Bevy bins by the mesh asset id
                                                              // (`mesh2d/material.rs:874`), not by the material.
                                                              .asset_id = mesh_instance_it->second.mesh_asset_id,
                                                              .material_bind_group_id = bind_group_id.value},
                    std::pair{visible_render_entity, visible_main_entity}, epix::render::phase::InputUniformIndex{},
                    phase_type, current_change_tick);
            } else if (std::holds_alternative<AlphaMode2dMask>(alpha_mode)) {
                alpha_mask_phase->second.add(
                    batch_set_key,
                    epix::core_graph::core_2d::AlphaMask2DBinKey{.pipeline_id = pipeline_id,
                                                                 .draw_func   = material->properties.draw_function_id,
                                                                 // Bevy bins by the mesh asset id
                                                                 // (`mesh2d/material.rs:892`).
                                                                 .asset_id = mesh_instance_it->second.mesh_asset_id,
                                                                 .material_bind_group_id = bind_group_id.value},
                    std::pair{visible_render_entity, visible_main_entity}, epix::render::phase::InputUniformIndex{},
                    phase_type, current_change_tick);
            } else {
                transparent_phase->second.add(epix::core_graph::core_2d::Transparent2D{
                    .representative_entity = {visible_render_entity, visible_main_entity},
                    .depth                 = mesh_z + material->properties.depth_bias,
                    .pipeline_id           = pipeline_id,
                    .draw_func             = material->properties.draw_function_id,
                    .batch_range_value     = {0u, 1u},
                    .extra_index_value     = epix::render::phase::PhaseItemExtraIndex::None,
                    .extracted_index       = std::numeric_limits<std::size_t>::max(),
                    .indexed_value         = mesh->indexed(),
                });
            }
        }
    }
}

template <typename M>
void epix::sprite_render::Material2dPlugin<M>::attach(epix::app::App& app) {
    // Bevy `Material2dPlugin::build`'s main-world half (material.rs:275-283):
    // `app.init_asset::<M>()` first, since every material is an `Asset`. Epix's
    // idiom for that is `assets::app_register_asset`, as `ShaderPlugin::attach`
    // does for `Shader`.
    epix::assets::app_register_asset<M>(app);
    // Bevy `Material2dPlugin::build` also adds `RenderAssetPlugin<PreparedMaterial2d<M>>`
    // (material.rs:285), which is what creates the render-world
    // `RenderAssets<PreparedMaterial2d<M>>` the queue system reads.
    app.add_plugins(epix::render::RenderAssetPlugin<PreparedMaterial2d<M>>{});
    auto& world = app.world_mut();
    world.init_resource<EntitiesNeedingSpecialization<M>>();
    app.add_systems(epix::app::PostUpdate, epix::ecs::into(check_entities_needing_specialization<M>)
                                               .after(epix::assets::AssetSystems::WriteEvents)
                                               .set_name("check entities needing material2d specialization"));
}

template <typename M>
void epix::sprite_render::Material2dPlugin<M>::ready(epix::app::App& app) {
    // Bevy `Material2dPlugin::build`'s render-world half (material.rs:285-316).
    auto render_app = app.get_sub_app_mut(epix::render::Render);
    if (!render_app) return;
    auto& render = render_app->get();

    render.world_mut().init_resource<EntitySpecializationTickPair<M>>();
    render.world_mut().init_resource<SpecializedMaterial2dPipelineCache<M>>();
    render.world_mut().init_resource<RenderMaterial2dInstances<M>>();
    render.world_mut().init_resource<epix::render::SpecializedMeshPipelines<Material2dPipeline<M>>>();

    // The material's draw sequence, one registration per phase it can land in
    // (Bevy `add_render_command::<P, DrawMaterial2d<M>>`, `material.rs`). No id
    // is kept: `prepare_asset` resolves the draw function from the phase's own
    // `DrawFunctions` under the same `DrawMaterial2d<M, P>` name.
    epix::render::phase::app_add_render_command<
        epix::sprite_render::DrawMaterial2d<M, epix::core_graph::core_2d::Opaque2D>>(render);
    epix::render::phase::app_add_render_command<
        epix::sprite_render::DrawMaterial2d<M, epix::core_graph::core_2d::AlphaMask2D>>(render);
    epix::render::phase::app_add_render_command<
        epix::sprite_render::DrawMaterial2d<M, epix::core_graph::core_2d::Transparent2D>>(render);

    render.add_systems(epix::render::RenderStartup, epix::ecs::into(init_material_2d_pipeline<M>)
                                                        .after(init_mesh_2d_pipeline)
                                                        .set_name("init material 2d pipeline"));
    render.add_systems(epix::render::ExtractSchedule,
                       epix::ecs::into(extract_entities_needs_specialization<M>, extract_mesh_materials_2d<M>));
    render.add_systems(epix::render::Render, epix::ecs::into(specialize_material2d_meshes<M>)
                                                 .in_set(epix::render::RenderSystems::PrepareMeshes)
                                                 .set_name("specialize material2d meshes"));
    render.add_systems(epix::render::Render, epix::ecs::into(queue_material2d_meshes<M>)
                                                 .in_set(epix::render::RenderSystems::QueueMeshes)
                                                 .set_name("queue material2d meshes"));
}

// `TilemapChunkMaterialPlugin` lives in its own translation unit
// (`tilemap_chunk_material.cpp`), which cannot see these template bodies, so the
// one 2D material type defined outside this file is instantiated here.
template struct epix::sprite_render::Material2dPlugin<epix::sprite_render::TilemapChunkMaterial>;

void epix::sprite_render::ColorMaterialPlugin::attach(epix::app::App& app) {
    // Bevy `ColorMaterialPlugin::build` (color_material.rs:14-32): publish the
    // shader, install the material plugin, and insert the default material.
    register_color_material_shader(app.world_mut());
    app.add_plugins(Material2dPlugin<ColorMaterial>{});
    app.world_mut().resource_mut<epix::assets::Assets<ColorMaterial>>().emplace(
        ColorMaterial{.color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)});
}

template <typename M>
void epix::sprite_render::extract_mesh_materials_2d(
    epix::ecs::ResMut<RenderMaterial2dInstances<M>> material_instances,
    epix::app::Extract<epix::ecs::Query<
        epix::ecs::Item<epix::ecs::Entity, const epix::camera::ViewVisibility&, const MeshMaterial2d<M>&>,
        epix::ecs::Filter<epix::ecs::Or<epix::ecs::Modified<epix::camera::ViewVisibility>,
                                        epix::ecs::Modified<MeshMaterial2d<M>>>>>> changed_meshes,
    epix::app::Extract<epix::ecs::RemovedComponents<MeshMaterial2d<M>>> removed_materials) {
    // Bevy `extract_mesh_materials_2d` (material.rs:330-378).
    for (auto&& [entity, view_visibility, material] : changed_meshes.iter()) {
        const epix::render::sync_world::MainEntity main_entity{entity};
        if (view_visibility.get()) {
            material_instances->instances.insert_or_assign(main_entity, material.id());
        } else {
            material_instances->instances.erase(main_entity);
        }
    }
    for (auto entity : removed_materials.read()) {
        // Only remove when the query above did not already handle this entity:
        // a component removed and re-added in the same frame appears in both.
        if (!changed_meshes.contains(entity)) {
            material_instances->instances.erase(epix::render::sync_world::MainEntity{entity});
        }
    }
}

template <typename M>
void epix::sprite_render::check_entities_needing_specialization(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity>,
                     epix::ecs::Filter<epix::ecs::Or<epix::ecs::Modified<epix::mesh::Mesh2d>,
                                                     epix::assets::AssetChanged<epix::mesh::Mesh2d>,
                                                     epix::ecs::Modified<MeshMaterial2d<M>>,
                                                     epix::assets::AssetChanged<MeshMaterial2d<M>>>,
                                       epix::ecs::With<MeshMaterial2d<M>>>> needs_specialization,
    epix::ecs::ResMut<EntitiesNeedingSpecialization<M>> entities_needing_specialization) {
    // Bevy `check_entities_needing_specialization` (material.rs:664-689).
    entities_needing_specialization->entities.clear();
    for (auto&& [entity] : needs_specialization.iter()) {
        entities_needing_specialization->entities.push_back(entity);
    }
}

template <typename M>
void epix::sprite_render::extract_entities_needs_specialization(
    epix::app::Extract<epix::ecs::Res<EntitiesNeedingSpecialization<M>>> entities_needing_specialization,
    epix::ecs::ResMut<EntitySpecializationTickPair<M>> entity_specialization_ticks,
    epix::app::Extract<epix::ecs::RemovedComponents<MeshMaterial2d<M>>> removed_mesh_material_components,
    epix::ecs::ResMut<SpecializedMaterial2dPipelineCache<M>> specialized_material2d_pipeline_cache,
    epix::ecs::Query<epix::ecs::Item<const epix::render::sync_world::MainEntity&>,
                     epix::ecs::With<epix::render::view::ExtractedView>> views,
    epix::ecs::SystemChangeTick ticks) {
    // Bevy `extract_entities_needs_specialization` (material.rs:568-593).
    for (auto entity : removed_mesh_material_components.read()) {
        const epix::render::sync_world::MainEntity main_entity{entity};
        entity_specialization_ticks->entities.erase(main_entity);
        for (auto&& [view] : views.iter()) {
            if (auto it = specialized_material2d_pipeline_cache->map.find(view);
                it != specialized_material2d_pipeline_cache->map.end()) {
                it->second.map.erase(main_entity);
            }
        }
    }
    for (auto entity : entities_needing_specialization->entities) {
        entity_specialization_ticks->entities.insert_or_assign(epix::render::sync_world::MainEntity{entity},
                                                               ticks.this_run());
    }
}

template <typename M>
void epix::sprite_render::init_material_2d_pipeline(epix::ecs::Commands commands,
                                                    epix::ecs::Res<wgpu::Device> device,
                                                    epix::ecs::Res<epix::assets::AssetServer> asset_server,
                                                    epix::ecs::Res<Mesh2dPipeline> mesh2d_pipeline) {
    // Bevy `init_material_2d_pipeline` (material.rs:480-500).
    (void)device;
    commands.insert_resource(Material2dPipeline<M>{
        .mesh2d_pipeline   = mesh2d_pipeline.get(),
        .material2d_layout = epix::render::BindGroupLayoutDescriptor()
                                 .set_label(std::string(epix::render::render_resource::AsBindGroup<M>::label()))
                                 .set_entries(epix::render::render_resource::AsBindGroup<M>::layout_entries()),
        .vertex_shader     = load_material_shader(M::vertex_shader(), asset_server.get()),
        .fragment_shader   = load_material_shader(M::fragment_shader(), asset_server.get()),
    });
}
