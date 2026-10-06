#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#endif

#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/spot_light.hpp>

namespace epix::pbr {

/** @brief Matches anything that is a light: a point, spot or directional light (Bevy
 *  `WithLight`, `bevy_light/src/lib.rs:209`).
 *
 *  An alias rather than a marker component, as in Bevy: it is the filter the light
 *  systems share, so it lives where its shape can change without every caller
 *  spelling out the three-way `Or`. */
EPIX_EXPORT using WithLight = ecs::Or<ecs::With<PointLight>, ecs::With<SpotLight>, ecs::With<DirectionalLight>>;

/** @brief Add to a mesh to make it not cast shadows (Bevy `NotShadowCaster`,
 *  `bevy_light/src/lib.rs:211-214`).
 *
 *  A marker, and a filter rather than a field: the light-visibility systems query
 *  meshes `Without<NotShadowCaster>`, so a mesh carrying it is never added to a
 *  light's visible list and so never reaches a shadow map. That is why it cannot be
 *  a flag on the mesh - the query shape depends on it.
 *
 *  Note the scope: this is about *casting*. A mesh can cast shadows and not receive
 *  them, or the reverse. */
EPIX_EXPORT struct NotShadowCaster {};

/** @brief Add to a mesh to make it not receive shadows (Bevy `NotShadowReceiver`,
 *  `bevy_light/src/lib.rs:215-222`).
 *
 *  Kept beside the caster marker because Bevy pairs them and they are read in the
 *  same place; unlike `NotShadowCaster` this one is consumed by the mesh pipeline's
 *  key rather than by a query filter. */
EPIX_EXPORT struct NotShadowReceiver {};

/** @brief Add to a mesh using a PBR material with `diffuse_transmission > 0.0` to
 *  make it receive shadows on its diffuse-transmission lobe, i.e. its backside
 *  (Bevy `TransmittedShadowReceiver`, `bevy_light/src/lib.rs:223-232`).
 *
 *  Not enabled by default, because it needs `StandardMaterial::thickness` to be set
 *  up (and possibly a baked thickness texture) so the mesh does not shadow itself.
 *  `NotShadowReceiver` overrides this one: a mesh carrying both receives neither
 *  regular nor diffusely transmitted shadows.
 *
 *  The mesh extraction reads it as a `Has<..>` and turns it into
 *  `MeshFlags::TRANSMITTED_SHADOW_RECEIVER`, so the shader gets it through the
 *  uniform's flags word rather than through a query filter. */
EPIX_EXPORT struct TransmittedShadowReceiver {};

}  // namespace epix::pbr
