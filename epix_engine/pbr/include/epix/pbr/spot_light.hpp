#pragma once

#ifndef EPIX_CXX_MODULE
#include <cmath>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/color.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::pbr {

/** @brief A light that emits in a given direction from a central point (Bevy
 *  `SpotLight`, `bevy_light/src/spot_light.rs:26-155`): a point light in a
 *  perfectly absorbent housing. The direction comes from the transform, so
 *  `Transform::looking_at` is the way to aim it.
 *
 *  As with the other lights, the colour is an `epix::color::Color` as in Bevy; the
 *  required components and `ClusterVisibilityClass` hook are
 *  registered the Epix way from the owning plugin, and the feature-gated
 *  `soft_shadows_enabled` field is not ported because the PCSS experiment is not
 *  part of the pinned feature set. */
EPIX_EXPORT struct SpotLight {
    /** @brief The colour of the light; white by default. */
    ::epix::color::Color color = ::epix::color::Color::WHITE;
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

    /** @brief Bevy's `#[require(Frustum, VisibleMeshEntities, Transform, Visibility,
     *  VisibilityClass)]` (`bevy_light/src/spot_light.rs:24`).
     *
     *  Registered the way this port expresses requirements (`camera/src/camera.cpp:163`,
     *  `point_light.hpp`). Without them a spawned light carries no `Visibility`, so no
     *  `ViewVisibility`, and `assign_objects_to_clusters` never sees it: the light is not
     *  clustered and casts nothing - the bug `PBR-891` fixed for point lights, and the
     *  same audit found here. Bevy's `ClusterVisibilityClass` `on_add` hook is not
     *  ported, since this port has none. */
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<::epix::transform::Transform>(
            [] { return ::epix::transform::Transform{}; });
        registrator.template register_required<::epix::camera::Visibility>(
            [] { return ::epix::camera::Visibility{}; });
        registrator.template register_required<::epix::camera::VisibilityClass>(
            [] { return ::epix::camera::VisibilityClass{}; });
        registrator.template register_required<::epix::camera::Frustum>(
            [] { return ::epix::camera::Frustum{}; });
        registrator.template register_required<::epix::camera::VisibleMeshEntities>(
            [] { return ::epix::camera::VisibleMeshEntities{}; });
    }
};

/** @brief A right-handed orthonormal basis from a unit Z vector (Bevy
 *  `orthonormalize`, `bevy_light/src/spot_light.rs:164-175`).
 *
 *  Bevy reproduces the fragment shader's construction here on purpose, so the
 *  CPU and GPU bases cannot drift apart if glam's own helper changes; the same
 *  arithmetic is written out for the same reason. */
EPIX_EXPORT inline glm::mat3 orthonormalize(glm::vec3 z_basis) noexcept {
    const float sign = std::copysign(1.0f, z_basis.z);
    const float a    = -1.0f / (sign + z_basis.z);
    const float b    = z_basis.x * z_basis.y * a;
    const glm::vec3 x_basis{1.0f + sign * z_basis.x * z_basis.x * a, sign * b, -sign * z_basis.x};
    const glm::vec3 y_basis{b, sign + z_basis.y * z_basis.y * a, -z_basis.y};
    return glm::mat3{x_basis, y_basis, z_basis};
}

/** @brief The orthonormal basis a spot light's shadow view is built from (Bevy
 *  `spot_light_world_from_view`, `bevy_light/src/spot_light.rs:179-185`).
 *
 *  `orthonormalize` applied to the light's backward axis - Bevy's
 *  `transform.back()`, which is the normalised local +Z column of Epix's
 *  matrix-only `GlobalTransform` - placed at the light's translation. Scale is
 *  deliberately not applied, as in Bevy. */
EPIX_EXPORT inline glm::mat4 spot_light_world_from_view(const ::epix::transform::GlobalTransform& transform) noexcept {
    const glm::vec3 fwd_dir = glm::normalize(glm::vec3(transform.matrix[2]));
    glm::mat4 world_from_view{orthonormalize(fwd_dir)};
    world_from_view[3] = glm::vec4(glm::vec3(transform.matrix[3]), 1.0f);
    return world_from_view;
}

/** @brief The projection a spot light's shadow view uses (Bevy
 *  `spot_light_clip_from_view`, `bevy_light/src/spot_light.rs:187-190`): the
 *  spot's field of view is **twice** the angle from its centre to the outer
 *  edge, with a square aspect and the light's shadow map near plane. */
EPIX_EXPORT inline glm::mat4 spot_light_clip_from_view(float angle, float near_z) {
    return camera::perspective_infinite_reverse_rh(angle * 2.0f, 1.0f, near_z);
}

/** @brief Add to a `SpotLight` to apply a texture mask to it (Bevy
 *  `SpotLightTexture`, `bevy_light/src/spot_light.rs:192-202`), which is what
 *  produces window-shadow, gobo/cookie and soft-falloff effects. Only the R
 *  channel of the image is read.
 *
 *  As with the probe maps, Epix's default is the engine's default image rather
 *  than a null handle, because `assets::Handle` has no default constructor
 *  (PBR-613). */
EPIX_EXPORT struct SpotLightTexture {
    /** @brief The texture image; only the R channel is read. Keep its border
     *  entirely black, or the light leaks past the mask. */
    assets::Handle<image::Image> image = image::DEFAULT_IMAGE_HANDLE;

    /** @brief Bevy's `#[require(SpotLight)]`. */
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<SpotLight>([] { return SpotLight{}; });
    }
};

}  // namespace epix::pbr
