#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <epix/camera/visibility.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <epix/common.hpp>
#endif
#include <epix/pbr/light_meta.hpp>

namespace epix::pbr {

/** @brief Which of a point light's cube faces a mesh is visible to (Bevy
 *  `check_point_light_mesh_visibility`, `bevy_light/src/lib.rs:565-590`).
 *
 *  One flag per face. The cases, in Bevy's order:
 *
 *  - **No AABB or no transform**: visible to every face. There is nothing to cull
 *    against, and the conservative answer is "visible" - the opposite of reading a
 *    missing bound as "nowhere", which would silently drop the mesh from every
 *    shadow map.
 *  - **`NoFrustumCulling`**: also visible to every face, but the sphere test is
 *    skipped as well, so the light's range does not bound it either.
 *  - **Otherwise**: a cheap sphere-versus-OBB test against the light's sphere first,
 *    which prunes everything outside the light's range without touching the six
 *    faces; then each face's frustum is tested against the mesh's oriented box.
 *
 *  `light_sphere` is the light's range as a sphere, so an object beyond it is visible
 *  to no face however close to a face's axis it sits. */
EPIX_EXPORT inline std::array<bool, POINT_LIGHT_CUBE_FACE_COUNT> point_light_face_visibility(
    const ::epix::camera::Sphere& light_sphere,
    const std::array<::epix::camera::Frustum, POINT_LIGHT_CUBE_FACE_COUNT>& cubemap_frusta,
    const std::optional<::epix::camera::Aabb>& aabb, const std::optional<glm::mat4>& world_from_local,
    bool no_frustum_culling) {
    std::array<bool, POINT_LIGHT_CUBE_FACE_COUNT> visible{};

    if (!aabb.has_value() || !world_from_local.has_value()) {
        visible.fill(true);
        return visible;
    }
    if (no_frustum_culling) {
        visible.fill(true);
        return visible;
    }
    if (!light_sphere.intersects_obb(*aabb, *world_from_local)) {
        return visible;  // Outside the light's range: no face.
    }

    for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
        visible[face] = cubemap_frusta[face].intersects_obb(*aabb, *world_from_local, true, true);
    }
    return visible;
}

/** @brief Whether a mesh is visible to a spot light (Bevy
 *  `check_point_light_mesh_visibility`'s spot-light branch,
 *  `bevy_light/src/lib.rs:656-674`).
 *
 *  The same two conservative cases as the point-light version, for the same reason:
 *  with no AABB or no transform there is nothing to cull against, and
 *  `NoFrustumCulling` skips the tests deliberately. Otherwise the light's range
 *  bounds it first - a spot light's cone test cannot help with an object beyond the
 *  light's reach - and then the cone's frustum is tested against the mesh's oriented
 *  box.
 *
 *  One frustum rather than six, which is the whole difference from the point-light
 *  case: a spot light has a single cone. */
EPIX_EXPORT inline bool spot_light_mesh_visible(const ::epix::camera::Sphere& light_sphere,
                                               const ::epix::camera::Frustum& frustum,
                                               const std::optional<::epix::camera::Aabb>& aabb,
                                               const std::optional<glm::mat4>& world_from_local,
                                               bool no_frustum_culling) {
    if (!aabb.has_value() || !world_from_local.has_value()) return true;
    if (no_frustum_culling) return true;
    if (!light_sphere.intersects_obb(*aabb, *world_from_local)) return false;
    return frustum.intersects_obb(*aabb, *world_from_local, true, true);
}

}  // namespace epix::pbr
