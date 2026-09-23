#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh.hpp>
#include <epix/render/binned_phase.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/view.hpp>
#include <epix/render/visibility_range.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_set>
#include <vector>
#include <epix/common.hpp>
#endif

#include <epix/pbr/light_markers.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/pbr/prepass.hpp>

namespace epix::pbr {

/** @brief All the data the GPU mesh extraction needs from a mesh entity (Bevy
 *  `GpuMeshExtractionQuery`, `bevy_pbr/src/render/mesh.rs:1460-1477`).
 *
 *  The fifteen items are Bevy's, in its order: the entity, the three required
 *  reads, the five optional reads (previous transform, lightmap, AABB, mesh tag and
 *  render layers) and the six `Has` markers. Bevy's query *data* type is this tuple;
 *  the change filter that selects which entities to visit is the system's. */
using GpuMeshExtractionItem =
    ecs::Item<ecs::Entity,
              const camera::ViewVisibility&,
              const transform::GlobalTransform&,
              ecs::Opt<const PreviousGlobalTransform&>,
              ecs::Opt<const Lightmap&>,
              ecs::Opt<const ::epix::camera::Aabb&>,
              const mesh::Mesh3d&,
              ecs::Opt<const mesh::MeshTag&>,
              ecs::Has<camera::NoFrustumCulling>,
              ecs::Has<NotShadowReceiver>,
              ecs::Has<TransmittedShadowReceiver>,
              ecs::Has<NotShadowCaster>,
              ecs::Has<render::phase::NoAutomaticBatching>,
              ecs::Has<camera::VisibilityRange>,
              ecs::Opt<const camera::RenderLayers&>>;

/** @brief The query over every mesh entity, used to re-extract the ones whose
 *  material was not ready (Bevy's `all_meshes_query`). */
using GpuMeshExtractionQuery = ecs::Query<GpuMeshExtractionItem>;

/** @brief The change filter: the entities whose mesh data may need a new input
 *  uniform this frame (Bevy `bevy_pbr/src/render/mesh.rs:1494-1509`).
 *
 *  Bevy's is an `Or<(..)>` of fourteen `Changed<T>` terms, one per component the
 *  extraction reads. Epix has no `Changed` filter - only `Added` and `Modified` -
 *  so each term is spelled as the pair Bevy's `Changed` stands for; that is the same
 *  idiom `visibility_range.hpp` uses for its `Changed<VisibilityRange>` early-out.
 *  The required `Mesh3d` read in the item list is what keeps non-mesh entities out. */
using GpuMeshExtractionChangedFilter =
    ecs::Or<ecs::Added<camera::ViewVisibility>,
            ecs::Modified<camera::ViewVisibility>,
            ecs::Added<transform::GlobalTransform>,
            ecs::Modified<transform::GlobalTransform>,
            ecs::Added<PreviousGlobalTransform>,
            ecs::Modified<PreviousGlobalTransform>,
            ecs::Added<Lightmap>,
            ecs::Modified<Lightmap>,
            ecs::Added<::epix::camera::Aabb>,
            ecs::Modified<::epix::camera::Aabb>,
            ecs::Added<mesh::Mesh3d>,
            ecs::Modified<mesh::Mesh3d>,
            ecs::Added<mesh::MeshTag>,
            ecs::Modified<mesh::MeshTag>,
            ecs::Added<camera::NoFrustumCulling>,
            ecs::Modified<camera::NoFrustumCulling>,
            ecs::Added<NotShadowReceiver>,
            ecs::Modified<NotShadowReceiver>,
            ecs::Added<TransmittedShadowReceiver>,
            ecs::Modified<TransmittedShadowReceiver>,
            ecs::Added<NotShadowCaster>,
            ecs::Modified<NotShadowCaster>,
            ecs::Added<render::phase::NoAutomaticBatching>,
            ecs::Modified<render::phase::NoAutomaticBatching>,
            ecs::Added<camera::VisibilityRange>,
            ecs::Modified<camera::VisibilityRange>,
            ecs::Added<mesh::SkinnedMesh>,
            ecs::Modified<mesh::SkinnedMesh>>;

/** @brief The entity set the system rebuilds each frame from the re-extraction
 *  requests and the component removals (Bevy's `Local<EntityHashSet>`). */
using GpuMeshExtractionReextractSet = std::unordered_set<ecs::Entity>;

/** @brief The optional item of a query row: Epix's `ecs::Opt<const T&>` yields this,
 *  which is what Bevy's `Option<&T>` becomes. */
template <typename T>
using OptRef = std::optional<std::reference_wrapper<const T>>;

/** @brief Turn one mesh entity into a queued instance builder (Bevy
 *  `extract_mesh_for_gpu_building`, `bevy_pbr/src/render/mesh.rs:1626-1705`).
 *
 *  Bevy passes the query row as one tuple; C++ cannot forward a structured binding,
 *  so the row arrives as its fifteen items and the four trailing arguments stay as
 *  they are. Everything else is Bevy's: an invisible entity is queued for removal
 *  and returns, the mesh flags come from the transform, the LOD index and the four
 *  component flags, the shared record from the GPU factory, and the culling data
 *  only when GPU culling is on. Epix's `Resource` wrappers are reached through
 *  `operator*`, which is where Bevy derefs them. */
EPIX_EXPORT void extract_mesh_for_gpu_building(ecs::Entity entity,
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
                                               bool any_gpu_culling);

/** @brief Extract every changed mesh into the GPU instance queues (Bevy
 *  `extract_meshes_for_gpu_building`, `bevy_pbr/src/render/mesh.rs:1487-1624`).
 *
 *  The render world must be using GPU uniform building, which the two
 *  `RenderMeshInstances` sides make a runtime property; Bevy panics otherwise and
 *  Epix throws. Visibility ranges arrive as a resource rather than through the
 *  query, because they are per-range data the extraction only looks up. */
EPIX_EXPORT void extract_meshes_for_gpu_building(
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
    ecs::Local<GpuMeshExtractionReextractSet> reextract_entities);

}  // namespace epix::pbr
