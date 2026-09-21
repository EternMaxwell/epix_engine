#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief A light that emits in a given direction from a central point (Bevy
 *  `SpotLight`, `bevy_light/src/spot_light.rs:26-155`): a point light in a
 *  perfectly absorbent housing. The direction comes from the transform, so
 *  `Transform::looking_at` is the way to aim it.
 *
 *  As with the other lights, Bevy's `bevy_color::Color` becomes a linear
 *  `glm::vec4`, its required components and `ClusterVisibilityClass` hook are
 *  registered the Epix way from the owning plugin, and the feature-gated
 *  `soft_shadows_enabled` field is not ported because the PCSS experiment is not
 *  part of the pinned feature set. */
EPIX_EXPORT struct SpotLight {
    /** @brief The colour of the light; white by default. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Luminous power in lumens. Bevy's default is a very large cinema
     *  light, bright at its default exposure and far too bright indoors. */
    float intensity = 1'000'000.0f;
    /** @brief Range in meters that this light illuminates. It also sets shadow
     *  map resolution, so it should be no larger than needed. */
    float range = 20.0f;
    /** @brief Radius of the simulated spherical emitter, which changes specular
     *  highlight size and soft shadow penumbra size. */
    float radius = 0.0f;
    /** @brief Whether this light casts shadows. */
    bool shadows_enabled = false;
    /** @brief Whether this light contributes diffuse lighting to meshes with
     *  lightmaps; the specular part is always considered. */
    bool affects_lightmapped_mesh_diffuse = true;
    /** @brief Shadow-acne versus Peter-Panning tradeoff; often scene-tuned. */
    float shadow_depth_bias = 0.02f;
    /** @brief Bias along the fragment's surface normal, scaled to the shadow
     *  map's texel size. */
    float shadow_normal_bias = 1.8f;
    /** @brief Distance from the light to the shadow map's near Z plane. */
    float shadow_map_near_z = 0.1f;
    /** @brief Angle from the light direction to the outer edge of the cone of
     *  effect; must be below `pi / 2`, which is a hemispherical spot light with
     *  very blocky shadows. */
    float outer_angle = 0.7853981633974483f;  // pi / 4
    /** @brief Angle from the light direction to the inner edge of the cone;
     *  light attenuates between the inner and outer angles. Must not exceed
     *  `outer_angle`. */
    float inner_angle = 0.0f;

    /** @brief Bevy's `SpotLight::DEFAULT_SHADOW_DEPTH_BIAS` (`:129`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_DEPTH_BIAS = 0.02f;
    /** @brief Bevy's `SpotLight::DEFAULT_SHADOW_NORMAL_BIAS` (`:130`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_NORMAL_BIAS = 1.8f;
    /** @brief Bevy's `SpotLight::DEFAULT_SHADOW_MAP_NEAR_Z` (`:131`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_MAP_NEAR_Z = 0.1f;
};

}  // namespace epix::pbr
