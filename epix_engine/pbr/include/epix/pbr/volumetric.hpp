#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief Add to a `DirectionalLight` that has shadows enabled to make
 *  volumetric fog interact with it, producing light shafts (Bevy
 *  `VolumetricLight`, `bevy_light/src/volumetric.rs:14-16`). */
EPIX_EXPORT struct VolumetricLight {};

/** @brief On a `Camera3d`, enables volumetric fog and volumetric lighting, also
 *  known as light shafts or god rays (Bevy `VolumetricFog`,
 *  `volumetric.rs:23-71`).
 *
 *  Its ambient light is separate from `AmbientLight` because an
 *  `EnvironmentMapLight` also counts as ambient for volumetric fog; the colour
 *  should approximate that map's average. Bevy stores `bevy_color::Color`s,
 *  which are linear `glm::vec4`s here. */
EPIX_EXPORT struct VolumetricFog {
    /** @brief The volumetric ambient light's colour; white by default. */
    glm::vec4 ambient_color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief The volumetric ambient light's brightness; 0.1 by default, and 0
     *  when there is no environment map. */
    float ambient_intensity = 0.1f;
    /** @brief The maximum distance, in meters, to offset the ray origin by,
     *  using interleaved gradient noise to vary the ray's start. Intended for
     *  temporal antialiasing. */
    float jitter = 0.0f;
    /** @brief The number of raymarching steps; higher is better quality and
     *  slower. 64 by default. */
    std::uint32_t step_count = 64;
};

/** @brief A volume of participating media (Bevy `FogVolume`,
 *  `volumetric.rs:76-159`). Bevy requires `Transform` and `Visibility`, which
 *  Epix registers from the plugin that owns the type.
 *
 *  Bevy's own doc comment for `scattering_asymmetry` says the default is 0.8
 *  while its `Default` implementation sets 0.5; the implementation is what is
 *  ported, and the discrepancy is recorded rather than silently resolved in the
 *  documentation's favour. */
EPIX_EXPORT struct FogVolume {
    /** @brief The fog's colour; white by default. It only shows once a
     *  `VolumetricLight` or ambient light lights the fog. */
    glm::vec4 fog_color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief How dark the fog is; 0.1 by default. */
    float density_factor = 0.1f;
    /** @brief An optional 3D voxel density texture. */
    std::optional<assets::Handle<image::Image>> density_texture;
    /** @brief Offset of the density texture in UVW coordinates, for scrolling a
     *  repeating texture; `ImageAddressMode::Repeat` makes that work. No effect
     *  without a density texture. Zero by default. */
    glm::vec3 density_texture_offset{0.0f, 0.0f, 0.0f};
    /** @brief What fraction of light the fog absorbs at each step; higher is
     *  darker. 0.3 by default. */
    float absorption = 0.3f;
    /** @brief What fraction of light is scattered toward and away from the
     *  viewer; 0.3 by default. */
    float scattering = 0.3f;
    /** @brief What fraction is scattered toward the camera rather than away, so
     *  higher values make light shafts appear as the camera faces their source.
     *  The implementation's default is 0.5. */
    float scattering_asymmetry = 0.5f;
    /** @brief A nonphysical colour applied to the light, for artistic effect;
     *  white by default. */
    glm::vec4 light_tint{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Scales the light by a fixed fraction; 1.0 by default, which is no
     *  adjustment. */
    float light_intensity = 1.0f;
};

}  // namespace epix::pbr
