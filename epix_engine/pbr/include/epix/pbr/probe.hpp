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

/** @brief An environment map filtered on the GPU at runtime (Bevy
 *  `GeneratedEnvironmentMapLight`, `bevy_light/src/probe.rs:116-141`). The
 *  source cubemap must be a power of two in size. */
EPIX_EXPORT struct GeneratedEnvironmentMapLight {
    /** @brief The source cubemap, filtered on the GPU at runtime. */
    assets::Handle<image::Image> environment_map = image::DEFAULT_IMAGE_HANDLE;
    /** @brief Scale applied to both maps; afterwards the values are in cd/m². */
    float intensity = 0.0f;
    /** @brief World-space rotation applied to the cubemap. */
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    /** @brief Whether this light contributes diffuse lighting to meshes with
     *  lightmaps. */
    bool affects_lightmapped_mesh_diffuse = true;
};

/** @brief Lets the atmosphere contribute environment lighting to a scene (Bevy
 *  `AtmosphereEnvironmentMapLight`, `probe.rs:152-173`). Attached to a `Camera3d`
 *  it lights the whole view; attached to a `LightProbe`, only that region. Bevy
 *  generates an environment map from the atmosphere and inserts a
 *  `GeneratedEnvironmentMapLight` for it. */
EPIX_EXPORT struct AtmosphereEnvironmentMapLight {
    /** @brief How bright the atmosphere's environment lighting is. Bevy's
     *  default of 1.0 matches the atmosphere's own intensity. */
    float intensity = 1.0f;
    /** @brief Whether the diffuse contribution affects lightmapped meshes. */
    bool affects_lightmapped_mesh_diffuse = true;
    /** @brief Cubemap resolution in pixels, which must be a power of two. */
    glm::uvec2 size{512, 512};
};

/** @brief An irradiance volume: a voxel grid of ambient cubes (Bevy
 *  `IrradianceVolume`, `probe.rs:182-221`). Bevy requires `LightProbe` on the
 *  entity and typically uses the volume through that probe's `Transform`.
 *
 *  Note the flag's name: Bevy spells it `affects_lightmapped_meshes` here, where
 *  the other lights spell it `affects_lightmapped_mesh_diffuse`. */
EPIX_EXPORT struct IrradianceVolume {
    /** @brief The 3D texture of ambient cubes, in the format
     *  `bevy_pbr::irradiance_volume` describes. */
    assets::Handle<image::Image> voxels = image::DEFAULT_IMAGE_HANDLE;
    /** @brief Scale applied to the volume's light; afterwards the values are in
     *  cd/m². */
    float intensity = 0.0f;
    /** @brief Whether the volume lights meshes with lightmaps; baking tools that
     *  already captured the indirect light set this false to avoid counting it
     *  twice. */
    bool affects_lightmapped_meshes = true;
};

}  // namespace epix::pbr
