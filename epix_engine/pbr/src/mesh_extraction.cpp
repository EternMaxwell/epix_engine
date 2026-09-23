#include <epix/pbr/mesh_extraction.hpp>

#include <spdlog/spdlog.h>
#include <stdexcept>

using namespace epix;
using namespace epix::pbr;

void epix::pbr::extract_mesh_for_gpu_building(ecs::Entity entity,
                                              const camera::ViewVisibility& view_visibility,
                                              const transform::GlobalTransform& transform,
                                              OptRef<PreviousGlobalTransform> previous_transform,
                                              OptRef<Lightmap> lightmap,
                                              OptRef<::epix::camera::Aabb> aabb,
                                              const mesh::Mesh3d& mesh,
                                              OptRef<mesh::MeshTag> tag,
                                              bool no_frustum_culling,
                                              bool not_shadow_receiver,
                                              bool transmitted_receiver,
                                              bool not_shadow_caster,
                                              bool no_automatic_batching,
                                              bool visibility_range,
                                              OptRef<camera::RenderLayers> render_layers,
                                              const render::view::RenderVisibilityRanges& render_visibility_ranges,
                                              const RenderMeshInstances& render_mesh_instances,
                                              RenderMeshInstanceGpuQueue& queue,
                                              bool any_gpu_culling) {
    const auto main_entity = render::sync_world::MainEntity{entity};
    if (!view_visibility.get()) {
        queue.remove(main_entity, any_gpu_culling);
        return;
    }

    // The LOD index only exists for entities with a visibility range, and it comes
    // from the range table rather than from the component.
    std::optional<std::uint16_t> lod_index;
    if (visibility_range) lod_index = render_visibility_ranges.lod_index_for_entity(main_entity);

    const MeshFlags mesh_flags = MeshFlags::from_components(transform, lod_index, no_frustum_culling,
                                                            not_shadow_receiver, transmitted_receiver);

    RenderMeshInstanceShared shared = RenderMeshInstanceShared::for_gpu_building(
        previous_transform ? std::addressof(previous_transform->get()) : nullptr, mesh,
        tag ? std::addressof(tag->get()) : nullptr, not_shadow_caster, no_automatic_batching,
        render_layers ? std::addressof(render_layers->get()) : nullptr,
        aabb ? std::addressof(aabb->get()) : nullptr);

    const glm::uvec2 lightmap_uv_rect =
        pack_lightmap_uv_rect(lightmap ? std::optional{lightmap->get().uv_rect} : std::nullopt);

    const std::optional<MeshCullingData> gpu_mesh_culling_data =
        any_gpu_culling ? std::optional{MeshCullingData::make(aabb ? std::optional{aabb->get()} : std::nullopt)} : std::nullopt;

    // Only a mesh that had a transform last frame can point at last frame's input
    // uniform, and only if it was extracted then.
    std::optional<std::uint32_t> previous_input_index;
    if (shared.flags.contains(RenderMeshInstanceFlags::HAS_PREVIOUS_TRANSFORM) &&
        !render_mesh_instances.is_cpu_building()) {
        const auto found = render_mesh_instances.gpu_instances.find(main_entity);
        if (found != render_mesh_instances.gpu_instances.end()) previous_input_index = found->second.current_uniform_index;
    }

    RenderMeshInstanceGpuBuilder builder{
        .shared             = shared,
        .world_from_local   = transform.matrix,
        .lightmap_uv_rect   = lightmap_uv_rect,
        .previous_input_index = previous_input_index,
        .mesh_flags         = mesh_flags,
    };

    queue.push(main_entity, std::move(builder), gpu_mesh_culling_data);
}

void epix::pbr::extract_meshes_for_gpu_building(
    ecs::ResMut<RenderMeshInstances> render_mesh_instances,
    ecs::Res<render::view::RenderVisibilityRanges> render_visibility_ranges,
    ecs::ResMut<RenderMeshInstanceGpuQueues> render_mesh_instance_queues,
    app::Extract<ecs::Query<GpuMeshExtractionItem, GpuMeshExtractionChangedFilter>> changed_meshes_query,
    app::Extract<ecs::RemovedComponents<PreviousGlobalTransform>> removed_previous_global_transform_query,
    app::Extract<ecs::RemovedComponents<Lightmap>> removed_lightmap_query,
    app::Extract<ecs::RemovedComponents<::epix::camera::Aabb>> removed_aabb_query,
    app::Extract<ecs::RemovedComponents<mesh::MeshTag>> removed_mesh_tag_query,
    app::Extract<ecs::RemovedComponents<camera::NoFrustumCulling>> removed_no_frustum_culling_query,
    app::Extract<ecs::RemovedComponents<NotShadowReceiver>> removed_not_shadow_receiver_query,
    app::Extract<ecs::RemovedComponents<TransmittedShadowReceiver>> removed_transmitted_receiver_query,
    app::Extract<ecs::RemovedComponents<NotShadowCaster>> removed_not_shadow_caster_query,
    app::Extract<ecs::RemovedComponents<render::phase::NoAutomaticBatching>> removed_no_automatic_batching_query,
    app::Extract<ecs::RemovedComponents<camera::VisibilityRange>> removed_visibility_range_query,
    app::Extract<ecs::RemovedComponents<mesh::SkinnedMesh>> removed_skinned_mesh_query,
    app::Extract<GpuMeshExtractionQuery> all_meshes_query,
    app::Extract<ecs::RemovedComponents<mesh::Mesh3d>> removed_meshes_query,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity>,
                            ecs::Filter<ecs::With<camera::Camera>, ecs::Without<render::view::NoIndirectDrawing>>>>
        gpu_culling_query,
    ecs::ResMut<MeshesToReextractNextFrame> meshes_to_reextract_next_frame,
    ecs::Local<GpuMeshExtractionReextractSet> reextract_entities) {
    reextract_entities->clear();

    // Any camera that is not drawing indirectly means some view culls on the GPU,
    // which is when per-instance culling data is worth collecting.
    const bool any_gpu_culling = [&gpu_culling_query] {
        auto range = gpu_culling_query.iter();
        return range.begin() != range.end();
    }();

    (*render_mesh_instance_queues).queue.init(any_gpu_culling);

    if (render_mesh_instances->is_cpu_building()) {
        // Bevy panics here: the system is only ever registered for the
        // GPU-uniform-building path, and running it otherwise would silently
        // populate the wrong side of the record set.
        throw std::runtime_error(
            "`extract_meshes_for_gpu_building` should only be called if we're using GPU `MeshUniform` building");
    }

    // Find all meshes that have changed, and record what is needed to build their
    // input uniforms. Bevy iterates in parallel with one queue per thread; Epix's
    // iteration is sequential into the single queue the resource holds.
    for (auto&& [entity, view_visibility, transform, previous_transform, lightmap, aabb, mesh, tag, no_frustum_culling,
                 not_shadow_receiver, transmitted_receiver, not_shadow_caster, no_automatic_batching, visibility_range,
                 render_layers] : changed_meshes_query.iter()) {
        extract_mesh_for_gpu_building(entity, view_visibility, transform, previous_transform, lightmap, aabb, mesh, tag,
                                      no_frustum_culling, not_shadow_receiver, transmitted_receiver, not_shadow_caster,
                                      no_automatic_batching, visibility_range, render_layers,
                                      render_visibility_ranges.get(), render_mesh_instances.get(),
                                      (*render_mesh_instance_queues).queue, any_gpu_culling);
    }

    // Process the meshes `collect_meshes_for_gpu_building` asked to retry - the ones
    // whose material was not prepared when they were first extracted - together with
    // every entity that lost a component the extraction reads. The retry set holds
    // main entities, which Bevy derefs to the entity it keys on; the removal readers
    // already yield entities.
    for (const auto entity : meshes_to_reextract_next_frame->entities) reextract_entities->insert(entity.id());
    const auto add_removed = [&reextract_entities](auto& removed) {
        for (const auto entity : removed.read()) reextract_entities->insert(entity);
    };
    add_removed(removed_previous_global_transform_query);
    add_removed(removed_lightmap_query);
    add_removed(removed_aabb_query);
    add_removed(removed_mesh_tag_query);
    add_removed(removed_no_frustum_culling_query);
    add_removed(removed_not_shadow_receiver_query);
    add_removed(removed_transmitted_receiver_query);
    add_removed(removed_not_shadow_caster_query);
    add_removed(removed_no_automatic_batching_query);
    add_removed(removed_visibility_range_query);
    add_removed(removed_skinned_mesh_query);

    for (const auto entity : *reextract_entities) {
        if (auto row = all_meshes_query.get(entity); row.has_value()) {
            auto&& [row_entity, view_visibility, transform, previous_transform, lightmap, aabb, mesh, tag,
                     no_frustum_culling, not_shadow_receiver, transmitted_receiver, not_shadow_caster,
                     no_automatic_batching, visibility_range, render_layers] = *row;
            extract_mesh_for_gpu_building(row_entity, view_visibility, transform, previous_transform, lightmap, aabb, mesh,
                                          tag, no_frustum_culling, not_shadow_receiver, transmitted_receiver,
                                          not_shadow_caster, no_automatic_batching, visibility_range, render_layers,
                                          render_visibility_ranges.get(), render_mesh_instances.get(),
                                          (*render_mesh_instance_queues).queue, any_gpu_culling);
        }
    }

    // Then record every mesh that became invisible. A mesh picked up above is not
    // queued for removal: a component can be removed and re-added in the same frame.
    for (const auto entity : removed_meshes_query.read()) {
        if (!changed_meshes_query.contains(entity) &&
            !meshes_to_reextract_next_frame->entities.contains(render::sync_world::MainEntity{entity})) {
            (*render_mesh_instance_queues).queue.remove(render::sync_world::MainEntity{entity}, any_gpu_culling);
        }
    }
}
