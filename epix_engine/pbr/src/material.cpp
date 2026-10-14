#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_extraction.hpp>
#include <epix/pbr/mesh_instance.hpp>

#include <utility>

namespace epix::pbr {

assets::UntypedAssetId RenderMaterialInstances::mesh_material(render::sync_world::MainEntity entity) const {
    const auto found = instances.find(entity);
    if (found == instances.end()) return DUMMY_MESH_MATERIAL;
    return found->second.asset_id;
}

std::optional<MaterialBindingId> RenderMaterialBindings::get(const assets::UntypedAssetId& asset_id) const {
    const auto found = bindings.find(asset_id);
    if (found == bindings.end()) return std::nullopt;
    return found->second;
}

void late_sweep_material_instances(ecs::ResMut<RenderMaterialInstances> material_instances,
                                   app::Extract<ecs::RemovedComponents<mesh::Mesh3d>> removed_meshes_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (const ecs::Entity entity : removed_meshes_query.read()) {
        const render::sync_world::MainEntity main_entity{entity};
        const auto found = material_instances->instances.find(main_entity);
        if (found != material_instances->instances.end() && found->second.last_change_tick != last_change_tick) {
            material_instances->instances.erase(found);
        }
    }

    material_instances->current_change_tick = ecs::Tick{last_change_tick.get() + 1};
}

void init_material_pipeline(ecs::Commands commands, ecs::Res<MeshPipeline> mesh_pipeline) {
    commands.insert_resource(MaterialPipeline{.mesh_pipeline = *mesh_pipeline});
}

void prepare_material_bind_groups(ecs::ResMut<MaterialBindGroupAllocators> allocators,
                                  ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
                                  ecs::Res<render::PipelineServer> pipeline_server,
                                  ecs::Res<render::texture::FallbackImage> fallback_image,
                                  ecs::Res<FallbackBindlessResources> fallback_resources) {
    // Bevy `prepare_material_bind_groups` (`material.rs:1907-1922`).
    for (auto& [type, allocator] : allocators->allocators) {
        (void)type;
        allocator.prepare_bind_groups(*render_device, *render_queue, *pipeline_server, *fallback_resources,
                                      *fallback_image);
    }
}

void write_material_bind_group_buffers(ecs::ResMut<MaterialBindGroupAllocators> allocators,
                                       ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue) {
    // Bevy `write_material_bind_group_buffers` (`material.rs:1929-1937`).
    for (auto& [type, allocator] : allocators->allocators) {
        (void)type;
        allocator.write_buffers(*render_device, *render_queue);
    }
}

void specialize_material_meshes(
    ecs::Res<render::RenderAssets<render::mesh::RenderMesh>> render_meshes,
    ecs::Res<render::erased_render_asset::ErasedRenderAssets<PreparedMaterial>> render_materials,
    ecs::Res<RenderMeshInstances> render_mesh_instances,
    ecs::Res<RenderMaterialInstances> render_material_instances,
    ecs::Res<RenderLightmaps> render_lightmaps,
    ecs::Res<render::view::RenderVisibilityRanges> render_visibility_ranges,
    ecs::Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3d>> opaque_render_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3d>> alpha_mask_render_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transmissive3d>> transmissive_render_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transparent3d>> transparent_render_phases,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::RenderVisibleEntities&>>
        views,
    ecs::Res<ViewKeyCache> view_key_cache, ecs::Res<EntitySpecializationTicks> entity_specialization_ticks,
    ecs::Res<ViewSpecializationTicks> view_specialization_ticks,
    ecs::ResMut<SpecializedMaterialPipelineCache> specialized_material_pipeline_cache,
    ecs::ResMut<render::SpecializedMeshPipelines<MaterialPipelineSpecializer>> pipelines,
    ecs::Res<MaterialPipeline> pipeline, ecs::Res<render::PipelineServer> pipeline_server,
    ecs::SystemChangeTick ticks) {
    // Bevy `specialize_material_meshes` (`material.rs:996-1148`).
    using MeshInstanceFlags = RenderMeshInstanceFlags;

    auto& cache = specialized_material_pipeline_cache->map;
    // Bevy records the retained id of every view it saw, then drops the caches of views that
    // expired by retaining only those. The port keeps the same set for the same purpose.
    std::unordered_set<render::view::RetainedViewEntity> all_views;

    for (auto&& [view_entity, view, visible_entities] : views.iter()) {
        all_views.insert(view.retained_view_entity);

        if (!transparent_render_phases->contains(view.retained_view_entity) &&
            !opaque_render_phases->phases.contains(view.retained_view_entity) &&
            !alpha_mask_render_phases->phases.contains(view.retained_view_entity) &&
            !transmissive_render_phases->contains(view.retained_view_entity)) {
            continue;
        }

        const auto view_key_it = view_key_cache->keys.find(view.retained_view_entity);
        if (view_key_it == view_key_cache->keys.end()) continue;

        const auto view_tick_it = view_specialization_ticks->ticks.find(view.retained_view_entity);
        if (view_tick_it == view_specialization_ticks->ticks.end()) continue;
        const auto view_tick = view_tick_it->second;

        auto& view_cache = cache[view.retained_view_entity];

        for (auto&& [visible_render_entity, visible_main_entity] :
             visible_entities.template get<epix::mesh::Mesh3d>()) {
            (void)visible_render_entity;
            const auto material_it = render_material_instances->instances.find(visible_main_entity);
            if (material_it == render_material_instances->instances.end()) continue;
            const auto mesh_instance = render_mesh_instances->render_mesh_queue_data(visible_main_entity);
            if (!mesh_instance.has_value()) continue;
            const auto entity_tick_it = entity_specialization_ticks->entities.find(visible_main_entity);
            if (entity_tick_it == entity_specialization_ticks->entities.end()) continue;

            const auto cached_it = view_cache.map.find(visible_main_entity);
            const bool needs_specialization =
                cached_it == view_cache.map.end() ||
                view_tick.newer_than(cached_it->second.first, ticks.this_run()) ||
                entity_tick_it->second.system_tick.newer_than(cached_it->second.first, ticks.this_run());
            if (!needs_specialization) continue;

            const auto* mesh = render_meshes->try_get(mesh_instance->record().mesh_asset_id);
            if (mesh == nullptr) continue;
            const auto* material = render_materials->get(material_it->second.asset_id);
            if (material == nullptr) continue;

            const auto view_key = view_key_it->second;
            auto mesh_pipeline_key_bits = material->properties->mesh_pipeline_key_bits;
            mesh_pipeline_key_bits |=
                alpha_mode_pipeline_key(material->properties->alpha_mode,
                                        render::view::msaa_from_samples(
                                            static_cast<std::uint32_t>(view_key.msaa_samples())));
            auto mesh_key = view_key | MeshPipelineKey::from_bits_retain(mesh->key_bits.bits()) |
                            mesh_pipeline_key_bits;

            if (const auto lightmap = render_lightmaps->render_lightmaps.find(visible_main_entity);
                lightmap != render_lightmaps->render_lightmaps.end()) {
                mesh_key |= MeshPipelineKey::LIGHTMAPPED;
                if (lightmap->second.bicubic_sampling) mesh_key |= MeshPipelineKey::LIGHTMAP_BICUBIC_SAMPLING;
            }

            if (render_visibility_ranges->entity_has_crossfading_visibility_ranges(visible_main_entity)) {
                mesh_key |= MeshPipelineKey::VISIBILITY_RANGE_DITHER;
            }

            if (view_key.contains(MeshPipelineKey::MOTION_VECTOR_PREPASS)) {
                // If the previous frame had skins or morph targets, note that.
                const auto flags = mesh_instance->record().flags;
                if (flags.contains(MeshInstanceFlags::HAS_PREVIOUS_SKIN)) mesh_key |= MeshPipelineKey::HAS_PREVIOUS_SKIN;
                if (flags.contains(MeshInstanceFlags::HAS_PREVIOUS_MORPH))
                    mesh_key |= MeshPipelineKey::HAS_PREVIOUS_MORPH;
            }

            const ErasedMaterialPipelineKey erased_key{
                .mesh_key     = mesh_key,
                .material_key = material->properties->material_key,
                .type_id      = material_it->second.asset_id.type,
            };
            const MaterialPipelineSpecializer specializer{
                .pipeline   = *pipeline,
                .properties = material->properties,
            };
            const auto pipeline_id = pipelines->specialize(*pipeline_server, specializer, erased_key, mesh->layout);
            if (!pipeline_id) {
                // Bevy logs the error; the port's error type has no formatter, so it reports
                // the failure without it rather than formatting an unformattable type.
                spdlog::error("[pbr] Failed to specialize a material's pipeline.");
                continue;
            }

            view_cache.map.insert_or_assign(visible_main_entity, std::pair{ticks.this_run(), *pipeline_id});
        }
    }

    // Bevy `retain`s the caches of the views that are still alive (material.rs:1145-1147).
    std::erase_if(cache, [&all_views](const auto& entry) { return !all_views.contains(entry.first); });
}

void MaterialsPlugin::attach(epix::app::App& app) {
    // Bevy `MaterialsPlugin::build` (`material.rs:281-335`), as far as the port has it.
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) return;
    auto& render = render_app->get();

    // `init_resource` rather than `insert_resource`: one registry serves every material
    // type, so a second material plugin must not wipe the first's contents.
    render.world_mut().init_resource<RenderMaterialInstances>();
    render.world_mut().init_resource<MaterialBindGroupAllocators>();
    render.world_mut().init_resource<EntitySpecializationTicks>();
    render.world_mut().init_resource<SpecializedMaterialPipelineCache>();
    render.world_mut().init_resource<render::SpecializedMeshPipelines<MaterialPipelineSpecializer>>();

    // Bevy registers the material-pipeline resource in `RenderStartup`, before anything
    // specializes against it (`material.rs:300`).
    render.add_systems(render::RenderStartup,
                       ecs::into(init_material_pipeline).set_name("init material pipeline"));

    // Bevy chains the pair and puts both in `PrepareBindGroups` (`material.rs:312-320`).
    render.add_systems(render::Render, ecs::into(prepare_material_bind_groups)
                                           .in_set(render::RenderSystems::PrepareBindGroups)
                                           .set_name("prepare material bind groups"));
    render.add_systems(render::Render, ecs::into(write_material_bind_group_buffers)
                                           .after(prepare_material_bind_groups)
                                           .in_set(render::RenderSystems::PrepareBindGroups)
                                           .set_name("write material bind group buffers"));

    // Bevy's orderings are what make this system's three inputs ready: the mesh assets it
    // reads are prepared, the GPU-built meshes are collected and the motion-vector flags are
    // set before it runs (`material.rs:304-308`).
    render.add_systems(render::Render, ecs::into(specialize_material_meshes)
                                           .in_set(render::RenderSystems::PrepareMeshes)
                                           .after(render::prepare_assets<render::mesh::RenderMesh>)
                                           .after(collect_meshes_for_gpu_building)
                                           .after(set_mesh_motion_vector_flags)
                                           .set_name("specialize material meshes"));
}

std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError>
MaterialPipelineSpecializer::specialize(const Key& key, const mesh::MeshVertexBufferLayoutRef& layout) const {
    // Bevy `SpecializedMeshPipeline for MaterialPipelineSpecializer`
    // (`material.rs:456-503`), in its order: the mesh pipeline first, then the two
    // shader defs, then the material's shader overrides, the bind group layout and
    // finally the bindless define.
    auto descriptor = pipeline.mesh_pipeline.specialize(key.mesh_key, layout);
    if (!descriptor) return std::unexpected(render::SpecializedMeshPipelineError{descriptor.error()});

    // The material's shader reads its bind group from this index, so the def and the
    // layout insertion below have to agree on it - which is why both use the port's
    // own constant rather than a literal.
    const shader::ShaderDefVal material_bind_group =
        shader::ShaderDefVal::from_uint("MATERIAL_BIND_GROUP", MATERIAL_BIND_GROUP_INDEX);
    descriptor->vertex.shader_defs.push_back(material_bind_group);
    if (descriptor->fragment) descriptor->fragment->shader_defs.push_back(material_bind_group);

    if (const auto* vertex_shader = properties->get_shader<MaterialVertexShader>()) {
        descriptor->vertex.shader = *vertex_shader;
    }
    if (const auto* fragment_shader = properties->get_shader<MaterialFragmentShader>()) {
        if (descriptor->fragment) descriptor->fragment->shader = *fragment_shader;
    }

    if (properties->material_layout) {
        descriptor->layouts.insert(descriptor->layouts.begin() + MATERIAL_BIND_GROUP_INDEX,
                                   *properties->material_layout);
    }

    if (properties->bindless) {
        const shader::ShaderDefVal bindless{std::string{"BINDLESS"}};
        descriptor->vertex.shader_defs.push_back(bindless);
        if (descriptor->fragment) descriptor->fragment->shader_defs.push_back(bindless);
    }

    return descriptor;
}

}  // namespace epix::pbr
