#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#endif

namespace epix::pbr {

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

}  // namespace epix::pbr
