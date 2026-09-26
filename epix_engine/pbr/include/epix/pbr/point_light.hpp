#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/pbr/light_consts.hpp>

namespace epix::pbr {

/** @brief A light that emits in all directions from a central point (Bevy
 *  `PointLight`, `bevy_light/src/point_light.rs:51-150`).
 *
 *  Bevy stores a `bevy_color::Color`; Epix has no colour crate, so the colour is
 *  a linear `glm::vec4`, and Bevy's required components (`CubemapFrusta`,
 *  `CubemapVisibleEntities`, `Transform`, `Visibility`, `VisibilityClass`) plus
 *  its `ClusterVisibilityClass` hook are expressed the way Epix registers them,
 *  from the plugin that owns the type. The feature-gated
 *  `soft_shadows_enabled` field is not ported because the PCSS experiment is
 *  not part of the pinned feature set. */
EPIX_EXPORT struct PointLight {
    /** @brief The colour of the light source. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Luminous power in lumens, i.e. the light emitted in all
     *  directions. */
    float intensity = light_consts::lumens::VERY_LARGE_CINEMA_LIGHT;
    /** @brief Cut-off for the light's area of effect; tune together with
     *  `intensity` to avoid hard cut-offs. */
    float range = 20.0f;
    /** @brief Radius of the simulated spherical emitter, which changes specular
     *  highlight size and soft shadow penumbra size. */
    float radius = 0.0f;
    /** @brief Whether this light casts shadows. */
    bool shadows_enabled = false;
    /** @brief Whether this light contributes diffuse lighting to meshes with
     *  lightmaps. Bevy's specular contribution is always considered, since
     *  specular light cannot be baked. */
    bool affects_lightmapped_mesh_diffuse = true;
    /** @brief Bias used when sampling shadow maps to avoid shadow acne. */
    float shadow_depth_bias = 0.08f;
    /** @brief Bias applied along the fragment's surface normal, scaled to the
     *  shadow map's texel size. */
    float shadow_normal_bias = 0.6f;
    /** @brief Distance from the light to the shadow map's near Z plane. */
    float shadow_map_near_z = 0.1f;

    /** @brief Bevy's `PointLight::DEFAULT_SHADOW_DEPTH_BIAS` (`:147`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_DEPTH_BIAS = 0.08f;
    /** @brief Bevy's `PointLight::DEFAULT_SHADOW_NORMAL_BIAS` (`:148`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_NORMAL_BIAS = 0.6f;
    /** @brief Bevy's `PointLight::DEFAULT_SHADOW_MAP_NEAR_Z` (`:149`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_MAP_NEAR_Z = 0.1f;
};

/** @brief Add to a `PointLight` to apply a texture mask to it (Bevy
 *  `PointLightTexture`, `bevy_light/src/point_light.rs:155-171`), which is what
 *  produces window-shadow, gobo/cookie and soft-falloff effects. Only the R
 *  channel of the image is read, and the image must be a packed cubemap in one of
 *  `CubemapLayout`'s formats.
 *
 *  As with the probe maps, Epix's default is the engine's default image rather
 *  than a null handle, because `assets::Handle` has no default constructor
 *  (PBR-613). */
EPIX_EXPORT struct PointLightTexture {
    /** @brief The texture image; only the R channel is read. Keep its border
     *  entirely black, or the light leaks past the mask. */
    assets::Handle<image::Image> image = image::DEFAULT_IMAGE_HANDLE;
    /** @brief The cubemap layout the image is packed in. */
    camera::CubemapLayout cubemap_layout = camera::CubemapLayout::CrossVertical;

    /** @brief Bevy's `#[require(PointLight)]`. */
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<PointLight>([] { return PointLight{}; });
    }
};

/** @brief Controls the resolution of point-light shadow maps (Bevy
 *  `PointLightShadowMap`, `point_light.rs:175-186`); `size` is the width and
 *  height of each of the cubemap's six faces and defaults to 1024. */
EPIX_EXPORT struct PointLightShadowMap {
    std::size_t size = 1024;
};

}  // namespace epix::pbr
