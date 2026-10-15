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

void queue_material_meshes(
    ecs::Res<render::erased_render_asset::ErasedRenderAssets<PreparedMaterial>> render_materials,
    ecs::Res<RenderMeshInstances> render_mesh_instances,
    ecs::Res<RenderMaterialInstances> render_material_instances, ecs::Res<render::mesh::MeshAllocator> mesh_allocator,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3d>> opaque_render_phases,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3d>> alpha_mask_render_phases,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transmissive3d>> transmissive_render_phases,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transparent3d>> transparent_render_phases,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::RenderVisibleEntities&>>
        views,
    ecs::Res<SpecializedMaterialPipelineCache> specialized_material_pipeline_cache) {
    // Bevy `queue_material_meshes` (`material.rs:1152-1350`).
    using core_graph::core_3d::Opaque3dBatchSetKey;
    using core_graph::core_3d::Opaque3dBinKey;
    using core_graph::core_3d::OpaqueNoLightmap3dBatchSetKey;
    using core_graph::core_3d::OpaqueNoLightmap3dBinKey;
    using core_graph::core_3d::Transmissive3d;
    using core_graph::core_3d::Transparent3d;

    for (auto&& [view_entity, view, visible_entities] : views.iter()) {
        (void)view_entity;
        // Bevy requires all four phases for a view before it queues anything into any of
        // them, so a view that is in one pass but not another is skipped whole.
        const auto transparent_phase = transparent_render_phases->find(view.retained_view_entity);
        if (transparent_phase == transparent_render_phases->end()) continue;
        const auto transmissive_phase = transmissive_render_phases->find(view.retained_view_entity);
        if (transmissive_phase == transmissive_render_phases->end()) continue;
        auto opaque_phase = opaque_render_phases->phases.find(view.retained_view_entity);
        if (opaque_phase == opaque_render_phases->phases.end()) continue;
        auto alpha_mask_phase = alpha_mask_render_phases->phases.find(view.retained_view_entity);
        if (alpha_mask_phase == alpha_mask_render_phases->phases.end()) continue;

        const auto view_cache_it = specialized_material_pipeline_cache->map.find(view.retained_view_entity);
        if (view_cache_it == specialized_material_pipeline_cache->map.end()) continue;
        const auto& view_cache = view_cache_it->second;

        const auto rangefinder = view.rangefinder3d();

        for (auto&& [visible_render_entity, visible_main_entity] :
             visible_entities.template get<epix::mesh::Mesh3d>()) {
            const auto cached_it = view_cache.map.find(visible_main_entity);
            if (cached_it == view_cache.map.end()) continue;
            const auto current_change_tick = cached_it->second.first;
            const auto pipeline_id         = cached_it->second.second;

            // A binned phase that already holds the entity up to date needs no work; the
            // sorted phases always re-queue, because their order depends on the camera.
            if (opaque_phase->second.validate_cached_entity(visible_main_entity, current_change_tick) ||
                alpha_mask_phase->second.validate_cached_entity(visible_main_entity, current_change_tick)) {
                continue;
            }

            const auto material_it = render_material_instances->instances.find(visible_main_entity);
            if (material_it == render_material_instances->instances.end()) continue;
            const auto mesh_instance = render_mesh_instances->render_mesh_queue_data(visible_main_entity);
            if (!mesh_instance.has_value()) continue;
            const auto* material = render_materials->get(material_it->second.asset_id);
            if (material == nullptr) continue;

            // The slabs the mesh's vertex and index data live in, as the batch-set key needs
            // them (Bevy `mesh_allocator.mesh_slabs`).
            const auto [vertex_slab, index_slab] = mesh_allocator->mesh_slabs(mesh_instance->record().mesh_asset_id);
            const auto entity_value              = std::pair{visible_render_entity, visible_main_entity};
            const auto material_bind_group_index = std::optional{material->binding.group.index};
            const auto batch_phase_type =
                render::phase::binned_render_phase_type_for_mesh(mesh_instance->record().should_batch(),
                                                                 *gpu_preprocessing_support);

            switch (material->properties->render_phase_type) {
                case RenderPhaseType::Transmissive: {
                    const float distance =
                        rangefinder.distance(mesh_instance->center) + material->properties->depth_bias;
                    const auto draw_function =
                        material->properties->get_draw_function<MainPassTransmissiveDrawFunction>();
                    if (!draw_function.has_value()) continue;
                    transmissive_phase->second.add(Transmissive3d{
                        .distance          = distance,
                        .pipeline_id       = pipeline_id,
                        .entity_value      = entity_value,
                        .draw_func         = *draw_function,
                        .batch_range_value = {0u, 1u},
                        .extra_index_value = render::phase::PhaseItemExtraIndex{},
                        .indexed_value     = index_slab.has_value(),
                    });
                    break;
                }
                case RenderPhaseType::Opaque: {
                    if (material->properties->render_method == OpaqueRendererMethod::Deferred) {
                        // Not queued, but still cached, so the entity is not re-examined every
                        // frame while the deferred pass draws it (Bevy `material.rs:1239-1246`).
                        opaque_phase->second.update_cache(visible_main_entity, std::nullopt, current_change_tick);
                        continue;
                    }
                    const auto draw_function = material->properties->get_draw_function<MainPassOpaqueDrawFunction>();
                    if (!draw_function.has_value()) continue;
                    opaque_phase->second.add(
                        Opaque3dBatchSetKey{
                            .pipeline_id               = pipeline_id,
                            .draw_func                 = *draw_function,
                            .material_bind_group_index = material_bind_group_index,
                            .vertex_slab               = vertex_slab.value_or(render::mesh::SlabId{}),
                            .index_slab                = index_slab,
                            .lightmap_slab =
                                mesh_instance->record().lightmap_slab_index.has_value()
                                    ? std::optional{mesh_instance->record().lightmap_slab_index->index}
                                    : std::nullopt,
                        },
                        Opaque3dBinKey{.asset_id = assets::UntypedAssetId{
                                           mesh_instance->record().mesh_asset_id}},
                        entity_value, mesh_instance->current_uniform_index, batch_phase_type, current_change_tick);
                    break;
                }
                case RenderPhaseType::AlphaMask: {
                    const auto draw_function =
                        material->properties->get_draw_function<MainPassAlphaMaskDrawFunction>();
                    if (!draw_function.has_value()) continue;
                    alpha_mask_phase->second.add(
                        OpaqueNoLightmap3dBatchSetKey{
                            .pipeline_id               = pipeline_id,
                            .draw_func                 = *draw_function,
                            .material_bind_group_index = material_bind_group_index,
                            .vertex_slab               = vertex_slab.value_or(render::mesh::SlabId{}),
                            .index_slab                = index_slab,
                        },
                        OpaqueNoLightmap3dBinKey{.asset_id = assets::UntypedAssetId{
                                                     mesh_instance->record().mesh_asset_id}},
                        entity_value, mesh_instance->current_uniform_index, batch_phase_type, current_change_tick);
                    break;
                }
                case RenderPhaseType::Transparent: {
                    const float distance =
                        rangefinder.distance(mesh_instance->center) + material->properties->depth_bias;
                    const auto draw_function =
                        material->properties->get_draw_function<MainPassTransparentDrawFunction>();
                    if (!draw_function.has_value()) continue;
                    transparent_phase->second.add(Transparent3d{
                        .distance          = distance,
                        .pipeline_id       = pipeline_id,
                        .entity_value      = entity_value,
                        .draw_func         = *draw_function,
                        .batch_range_value = {0u, 1u},
                        .extra_index_value = render::phase::PhaseItemExtraIndex{},
                        .indexed_value     = index_slab.has_value(),
                    });
                    break;
                }
            }
        }
    }
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
    // set before it runs (`material.rs:304-308`). The collect step is ordered *before* the
    // motion-vector flags here rather than alongside them: the port registers that system only
    // on the GPU instance path (PBR-16), so naming it from this schedule would be an ignored
    // dependency with a warning, and the order it exists for is already implied.
    render.add_systems(render::Render, ecs::into(specialize_material_meshes)
                                           .in_set(render::RenderSystems::PrepareMeshes)
                                           .after(render::prepare_assets<render::mesh::RenderMesh>)
                                           .after(set_mesh_motion_vector_flags)
                                           .set_name("specialize material meshes"));
    // Bevy queues in `QueueMeshes`, which the render schedule runs after `PrepareMeshes`
    // (`material.rs:309`), and the queue reads the cache that system filled.
    render.add_systems(render::Render, ecs::into(queue_material_meshes)
                                           .in_set(render::RenderSystems::QueueMeshes)
                                           .after(specialize_material_meshes)
                                           .set_name("queue material meshes"));
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
