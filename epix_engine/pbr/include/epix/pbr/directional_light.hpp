#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/pbr/light_consts.hpp>

namespace epix::pbr {

/** @brief A directional light (Bevy `DirectionalLight`,
 *  `bevy_light/src/directional_light.rs:70-157`): a good approximation of a
 *  source very far away, such as the sun. It shines along the transform's
 *  forward direction, which with a default transform is negative Z, and its
 *  illuminance is in lux rather than lumens because it lights every surface
 *  more or less the same way.
 *
 *  Bevy requires `Cascades`, `CascadesFrusta`, `CascadeShadowConfig`,
 *  `CascadesVisibleEntities`, `Transform`, `Visibility` and `VisibilityClass`
 *  and adds a `ClusterVisibilityClass` hook; Epix registers those the way it
 *  does elsewhere, from the plugin that owns the type. The feature-gated
 *  `soft_shadow_size` is not ported, since the PCSS experiment is not part of
 *  the pinned feature set. */
EPIX_EXPORT struct DirectionalLight {
    /** @brief The colour of the light; white by default. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Illuminance in lux (lumens per square meter). Bevy's default is
     *  ambient daylight, 10 000 lux. */
    float illuminance = light_consts::lux::AMBIENT_DAYLIGHT;
    /** @brief Whether this light casts shadows, via cascaded shadow maps. */
    bool shadows_enabled = false;
    /** @brief Whether this light contributes diffuse lighting to meshes with
     *  lightmaps; the specular part is always considered. */
    bool affects_lightmapped_mesh_diffuse = true;
    /** @brief Shadow-acne versus Peter-Panning tradeoff; often scene-tuned. */
    float shadow_depth_bias = 0.02f;
    /** @brief Bias along the fragment's surface normal, scaled to the shadow
     *  map's texel size. */
    float shadow_normal_bias = 1.8f;

    /** @brief Bevy's `DirectionalLight::DEFAULT_SHADOW_DEPTH_BIAS` (`:156`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_DEPTH_BIAS = 0.02f;
    /** @brief Bevy's `DirectionalLight::DEFAULT_SHADOW_NORMAL_BIAS` (`:157`). */
    EPIX_EXPORT static constexpr float DEFAULT_SHADOW_NORMAL_BIAS = 1.8f;

    /** @brief Bevy's `#[require(Cascades, CascadesFrusta, CascadeShadowConfig,
     *  CascadesVisibleEntities, Transform, Visibility, VisibilityClass)]`.
     *
     *  Registered the way this port expresses requirements (`camera/src/camera.cpp:163`,
     *  `point_light.hpp`). Without them a spawned light carries no `Visibility`, so no
     *  `ViewVisibility`, and `assign_objects_to_clusters` never sees it: the light is not
     *  clustered and casts nothing - the bug `PBR-891` fixed for point lights. Bevy's
     *  `ClusterVisibilityClass` `on_add` hook is not ported, since this port has none. */
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<::epix::transform::Transform>(
            [] { return ::epix::transform::Transform{}; });
        registrator.template register_required<::epix::camera::Visibility>(
            [] { return ::epix::camera::Visibility{}; });
        registrator.template register_required<::epix::camera::VisibilityClass>(
            [] { return ::epix::camera::VisibilityClass{}; });
        registrator.template register_required<Cascades>([] { return Cascades{}; });
        registrator.template register_required<::epix::camera::CascadesFrusta>(
            [] { return ::epix::camera::CascadesFrusta{}; });
        registrator.template register_required<CascadeShadowConfig>(
            [] { return CascadeShadowConfig{}; });
        registrator.template register_required<::epix::camera::CascadesVisibleEntities>(
            [] { return ::epix::camera::CascadesVisibleEntities{}; });
    }
};

/** @brief Controls the resolution of directional and spot light shadow maps
 *  (Bevy `DirectionalLightShadowMap`, `directional_light.rs:183-195`); `size` is
 *  the width and height of each cascade and defaults to 2048. Bevy requires it
 *  to be a power of two to avoid unstable cascade positioning, and the
 *  `validate_shadow_map_size` system in `light_systems.hpp` rounds it up when it
 *  is not. */
EPIX_EXPORT struct DirectionalLightShadowMap {
    std::size_t size = 2048;
};

/** @brief A texture mask applied to a directional light, for patterns such as
 *  window shadows or gobo/cookie effects (Bevy `DirectionalLightTexture`,
 *  `directional_light.rs:166-171`). Only the R channel is read. */
EPIX_EXPORT struct DirectionalLightTexture {
    /** @brief The texture image. Bevy has no default handle here and requires
     *  one to be supplied; Epix's `assets::Handle` has no default constructor
     *  either, so this defaults to the engine's default image - the convention
     *  the skybox component already uses. */
    assets::Handle<image::Image> image = image::DEFAULT_IMAGE_HANDLE;
    /** @brief Whether to tile the image infinitely, or use a single tile centred
     *  on the light's transform. */
    bool tiled = false;
};

}  // namespace epix::pbr
