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
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/shadow_phase.hpp>

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
    ecs::Query<ecs::Item<ecs::Ref<Cascades>, ecs::Ref<DirectionalLight>,
                         ecs::Ref<::epix::camera::ViewVisibility>, ecs::Mut<::epix::camera::CascadesFrusta>>,
                ecs::Without<::epix::camera::Camera>>
        lights);

/** @brief Clear each shadow-casting directional light's cascades (Bevy
 *  `clear_directional_light_cascades`, `bevy_light/src/cascade.rs:186-193`).
 *
 *  Runs before the cascades are rebuilt each frame, so a view that no longer has
 *  cascades does not keep the ones it had - the builder only inserts. Lights
 *  without shadows keep whatever they have, which is nothing, since the builder
 *  skips them too. */
EPIX_EXPORT void clear_directional_light_cascades(
    ecs::Query<ecs::Item<ecs::Ref<DirectionalLight>, ecs::Mut<Cascades>>> lights);

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
    ecs::Query<ecs::Item<ecs::Entity, ecs::Ref<::epix::transform::GlobalTransform>, ecs::Ref<::epix::camera::Projection>,
                         ecs::Ref<::epix::camera::Camera>>>
        views,
    ecs::Query<ecs::Item<ecs::Ref<::epix::transform::GlobalTransform>, ecs::Ref<DirectionalLight>,
                         ecs::Ref<CascadeShadowConfig>, ecs::Mut<Cascades>>>
        lights);

}  // namespace epix::pbr
