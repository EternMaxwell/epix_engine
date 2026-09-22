#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <epix/common.hpp>
#endif
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/light_cluster.hpp>
#include <epix/pbr/light_markers.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/probe.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/pbr/volumetric.hpp>

namespace epix::pbr {

/** @brief Rebuild the cubemap frusta of every point light that needs it (Bevy
 *  `update_point_light_frusta`, `bevy_light/src/point_light.rs:188-244`).
 *
 *  Meant to run after the lights are assigned to clusters, as Bevy's note says, and
 *  registered by the plugin that owns the type - like every other system here, the
 *  function is defined so it can be added to a schedule but is not scheduled on its
 *  own. A light is skipped when neither it nor the clustered set changed, when it
 *  casts no shadows, or when it is in no cluster (see
 *  `should_update_point_light_frusta`). */
EPIX_EXPORT void update_point_light_frusta(
    ecs::Res<GlobalVisibleClusterableObjects> global_lights,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Ref<::epix::transform::GlobalTransform>, ecs::Ref<PointLight>,
                         ecs::Mut<::epix::camera::CubemapFrusta>>>
        lights);

/** @brief Rebuild the cascade frusta of every directional light that needs it
 *  (Bevy `update_directional_light_frusta`,
 *  `bevy_light/src/directional_light.rs:206-241`).
 *
 *  Rebuilds from the light's `Cascades`, one frustum per cascade per view, with the
 *  near plane pushed out to infinity so a cascade culls nothing in front of the
 *  light. Unlike the point-light system this has no change detection: every
 *  shadow-casting, visible light is rebuilt each time it runs. */
EPIX_EXPORT void update_directional_light_frusta(
    ecs::Query<ecs::Item<const Cascades&, const DirectionalLight&, const ::epix::camera::ViewVisibility&,
                         ecs::Mut<::epix::camera::CascadesFrusta>>,
                ecs::Without<::epix::camera::Camera>>
        lights);

/** @brief Clear each shadow-casting directional light's cascades (Bevy
 *  `clear_directional_light_cascades`, `bevy_light/src/cascade.rs:186-193`).
 *
 *  Runs before the cascades are rebuilt each frame, so a view that no longer has
 *  cascades does not keep the ones it had - the builder only inserts. Lights
 *  without shadows keep whatever they have, which is nothing, since the builder
 *  skips them too - and because they are skipped, the write is a `Mut`. */
EPIX_EXPORT void clear_directional_light_cascades(
    ecs::Query<ecs::Item<const DirectionalLight&, ecs::Mut<Cascades>>> lights);

/** @brief Build every directional light's cascades for every active view (Bevy
 *  `build_directional_light_cascades`, `bevy_light/src/cascade.rs:195-253`).
 *
 *  The light matrix is built from the light's **rotation alone** with no
 *  translation - Epix's `GlobalTransform` carries only a matrix, so the rotation is
 *  recovered from its upper-left 3x3 with each column normalised. That matters:
 *  Bevy's comment is explicit that the matrix must have an orthogonal upper-left
 *  3x3 and zero translation for the shadows to be numerically stable, and a light's
 *  transform is free to carry scale. */
EPIX_EXPORT void build_directional_light_cascades(
    ecs::Res<DirectionalLightShadowMap> shadow_map,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const ::epix::camera::Projection&,
                         const ::epix::camera::Camera&>>
        views,
    ecs::Query<ecs::Item<const ::epix::transform::GlobalTransform&, const DirectionalLight&,
                         const CascadeShadowConfig&, ecs::Mut<Cascades>>>
        lights);

/** @brief Assign every clusterable object to the clusters of every view (Bevy
 *  `assign_objects_to_clusters`, `bevy_light/src/cluster/assign.rs:124-841`).
 *
 *  The whole flow: clear the global set and gather the clusterable objects -
 *  visible point lights, visible spot lights, probes when the device has storage
 *  buffers, decals when the platform supports them - then cut them to what a
 *  uniform buffer holds when there are no storage buffers, then per view build its
 *  cluster grid and hand each object to `assign_object_to_view_clusters`.
 *
 *  A view with no cluster configuration has its clusters cleared and its visible
 *  objects removed; a view with a zero-sized viewport is skipped with its clusters
 *  cleared. The per-cluster bindings themselves are sized by the function that owns
 *  the device, not here.
 *
 *  The view's clusters are written unconditionally, so they are a plain reference
 *  rather than a `Mut`; the visible-object list is likewise only ever appended to
 *  when present. Nothing here reads change ticks, so the reads are references too.
 *
 *  Runs after visibility and transform propagation, as Bevy schedules it. */
EPIX_EXPORT void assign_objects_to_clusters(
    ecs::Commands commands,
    ecs::ResMut<GlobalVisibleClusterableObjects> global_clusterable_objects,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const ::epix::camera::Camera&,
                         const ::epix::camera::Frustum&, const ClusterConfig&, Clusters&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<VisibleClusterableObjects&>>>
        views,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const PointLight&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const VolumetricLight&>,
                         const ::epix::camera::ViewVisibility&>>
        point_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const SpotLight&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const VolumetricLight&>,
                         const ::epix::camera::ViewVisibility&>>
        spot_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, ecs::Has<EnvironmentMapLight>>,
               ecs::With<LightProbe>>
        light_probes,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&>, ecs::With<ClusteredDecal>> decals,
    ecs::Local<std::vector<ClusterableObjectAssignmentData>> clusterable_objects,
    ecs::Local<std::vector<std::optional<::epix::camera::Sphere>>> cluster_spheres,
    ecs::Local<bool> warning_emitted,
    std::optional<ecs::Res<GlobalClusterSettings>> global_cluster_settings);

/** @brief Mark the meshes each visible light can see, for shadow mapping (Bevy
 *  `check_point_light_mesh_visibility`, `bevy_light/src/lib.rs:466-686`).
 *
 *  Despite the name this handles spot lights too, as Bevy's does: both are point-like
 *  lights whose shadow maps are built from what they can see, and the two branches
 *  differ only in six cube faces versus one cone.
 *
 *  For each light it clears the visible lists, skips the light entirely when shadows
 *  are disabled - a light without shadow mapping must have no visible entities, so its
 *  lists stay empty rather than stale - and then walks the candidate meshes, which
 *  the query has already filtered to those carrying a mesh and not carrying
 *  `NotShadowCaster`. A candidate is skipped when it is not inherited-visible, when
 *  the light's layers do not intersect the mesh's, or when the visibility range says
 *  it is in range of no view. What survives is offered to
 *  `point_light_face_visibility` or `spot_light_mesh_visible`, and a mesh visible to a
 *  face is marked visible and recorded for it.
 *
 *  Bevy iterates the entities of the views' `VisibleClusterableObjects` components and
 *  looks each up, deduplicating with a local set. The same set is used here to decide
 *  whether a light is visible to any view at all, which visits each light once instead
 *  of once per view - the same result, since the set is a union, without needing a
 *  by-entity query lookup. Bevy's parallel per-face queues are plain vectors here;
 *  again the same result, without the parallel-for machinery. */
EPIX_EXPORT void check_point_light_mesh_visibility(
    ecs::Query<ecs::Item<const VisibleClusterableObjects&>> visible_lights,
    ecs::Query<ecs::Item<ecs::Entity, const PointLight&, const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::CubemapFrusta&, ::epix::camera::CubemapVisibleEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>>>
        point_lights,
    ecs::Query<ecs::Item<ecs::Entity, const SpotLight&, const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::Frustum&, ::epix::camera::VisibleMeshEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>>>
        spot_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::camera::InheritedVisibility&, ::epix::camera::ViewVisibility&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const ::epix::camera::Aabb&>,
                         ecs::Opt<const ::epix::transform::GlobalTransform&>, ecs::Opt<const ::epix::camera::VisibilityRange&>,
                         ecs::Opt<const ::epix::camera::NoFrustumCulling&>, ecs::Opt<const ::epix::mesh::Mesh3d&>,
                         ecs::Opt<const NotShadowCaster&>, ecs::Opt<const DirectionalLight&>>>
        visible_entities,
    std::optional<ecs::Res<::epix::camera::VisibleEntityRanges>> visible_entity_ranges);

/** @brief Mark the meshes each directional light can see, per cascade (Bevy
 *  `check_dir_light_mesh_visibility`, `bevy_light/src/lib.rs:302-464`).
 *
 *  The per-view cascade lists are reconciled with the cascades the light has before
 *  anything is culled, then the light is skipped unless it casts shadows *and* is
 *  itself visible - the same pair of conditions the frusta system uses.
 *
 *  Three differences from the point and spot version, all Bevy's:
 *
 *  - The visibility-range test is per view rather than "any view", since a cascade
 *    exists for one view.
 *  - **Near-plane culling is disabled.** A shadow caster can lie in front of the near
 *    plane and still cast into the cascade, so `intersects_obb` is asked with
 *    `intersect_near` false and only the far side bounding it.
 *  - The meshes a light sees are marked visible through a **command** rather than
 *    directly, which is what lets this system run in parallel with the point and spot
 *    one instead of both needing mutable access to `ViewVisibility`.
 *
 *  A mesh with no AABB or no transform is visible to every cascade, as in the point
 *  case and for the same reason. */
EPIX_EXPORT void check_dir_light_mesh_visibility(
    ecs::Commands commands,
    ecs::Query<ecs::Item<const DirectionalLight&, const ::epix::camera::CascadesFrusta&,
                         ::epix::camera::CascadesVisibleEntities&, ecs::Opt<const ::epix::camera::RenderLayers&>,
                         const ::epix::camera::ViewVisibility&, ecs::Opt<const SpotLight&>>>
        directional_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::camera::InheritedVisibility&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const ::epix::camera::Aabb&>,
                         ecs::Opt<const ::epix::transform::GlobalTransform&>, ecs::Opt<const ::epix::camera::VisibilityRange&>,
                         ecs::Opt<const ::epix::camera::NoFrustumCulling&>, ecs::Opt<const ::epix::mesh::Mesh3d&>,
                         ecs::Opt<const NotShadowCaster&>, ecs::Opt<const DirectionalLight&>>>
        visible_entities,
    std::optional<ecs::Res<::epix::camera::VisibleEntityRanges>> visible_entity_ranges);

}  // namespace epix::pbr
