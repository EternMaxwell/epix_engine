#pragma once

#ifndef EPIX_CXX_MODULE
#include <glm/glm.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The world transform an entity had last frame (Bevy
 *  `PreviousGlobalTransform`, `bevy_pbr/src/prepass/mod.rs:209`).
 *
 *  Bevy wraps an `Affine3A` and keeps it current with
 *  `update_previous_global_transforms`, so motion vectors and the motion-vector
 *  prepass can be derived without re-reading the main world; Epix's convention for
 *  transforms is the project's `glm::mat4`, taken from `GlobalTransform::matrix`.
 *
 *  The mesh extraction only asks whether the component is present - it sets
 *  `HAS_PREVIOUS_TRANSFORM` from that - while the prepass and the mesh uniforms
 *  read the matrix itself. */
EPIX_EXPORT struct PreviousGlobalTransform {
    glm::mat4 matrix{1.0f};
};

}  // namespace epix::pbr
