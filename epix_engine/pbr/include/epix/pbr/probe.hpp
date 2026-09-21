#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <glm/gtc/quaternion.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief A marker for a light probe: a cuboid region that provides global
 *  illumination to every fragment inside it (Bevy `LightProbe`,
 *  `bevy_light/src/probe.rs:48-59`).
 *
 *  The range is conceptually a unit cube centred on the origin, scaled, rotated
 *  and translated by the entity's `Transform`. A light probe does nothing on its
 *  own; the entity also needs an `EnvironmentMapLight` or an `IrradianceVolume`.
 *
 *  Bevy requires `Transform` and `Visibility`, which Epix registers from the
 *  plugin that owns the component. */
EPIX_EXPORT struct LightProbe {};

/** @brief A pair of cubemaps describing the surroundings of an area (Bevy
 *  `EnvironmentMapLight`, `probe.rs:67-108`).
 *
 *  Bevy defaults both handles with `Handle::default()`; Epix's `assets::Handle`
 *  has no default constructor, so they default to the engine's default image,
 *  the convention the skybox and directional-light texture already use. */
EPIX_EXPORT struct EnvironmentMapLight {
    /** @brief The blurry cubemap of diffuse radiance. */
    assets::Handle<image::Image> diffuse_map = image::DEFAULT_IMAGE_HANDLE;
    /** @brief The typically sharper, mipmapped cubemap of specular radiance. */
    assets::Handle<image::Image> specular_map = image::DEFAULT_IMAGE_HANDLE;
    /** @brief Scale applied to both maps; afterwards the values are in cd/m². */
    float intensity = 0.0f;
    /** @brief World-space rotation applied to both cubemaps, for sources whose
     *  vertical axis is not Y. */
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    /** @brief Whether this environment contributes diffuse lighting to meshes
     *  with lightmaps; the specular part is always considered. */
    bool affects_lightmapped_mesh_diffuse = true;
};

}  // namespace epix::pbr
