#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/camera.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::pbr {

/** @brief An ambient light, which lights the entire scene equally (Bevy
 *  `AmbientLight`, `bevy_light/src/ambient_light.rs:12-29`). Added to a camera
 *  it overrides `GlobalAmbientLight`; Bevy expresses that with a required
 *  `Camera` component, registered here from the type's own hook.
 *
 *  Bevy stores a `bevy_color::Color`; Epix keeps the linear `glm::vec4` the light uniform
 *  consumes, a documented representation divergence left for a later pass now that
 *  `epix::color` exists. */
EPIX_EXPORT struct AmbientLight {
    /** @brief The light's colour, linear. Bevy's default is white. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief A direct scale factor multiplied with the colour before it reaches
     *  the shader; the result is in cd/m². Bevy's default is 80. */
    float brightness = 80.0f;
    /** @brief Whether this light affects meshes with lightmaps. Set false when
     *  the baking tool already baked the ambient light in, to avoid lighting
     *  twice. */
    bool affects_lightmapped_meshes = true;

    /** @brief Bevy's `#[require(Camera)]` (`bevy_light/src/ambient_light.rs:11`). A
     *  camera-level `AmbientLight` overrides `GlobalAmbientLight`, so the light only
     *  means anything on a camera entity; without the requirement a spawned
     *  `AmbientLight` would sit on an entity no view looks at. */
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<::epix::camera::Camera>([] { return ::epix::camera::Camera{}; });
    }
};

/** @brief The global ambient light, inserted by the light plugin and overridden
 *  by a camera-level `AmbientLight` (Bevy `GlobalAmbientLight`,
 *  `ambient_light.rs:59-96`). */
EPIX_EXPORT struct GlobalAmbientLight {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float brightness                = 80.0f;
    bool affects_lightmapped_meshes = true;

    /** @brief No ambient light at all (Bevy `GlobalAmbientLight::NONE`). */
    EPIX_EXPORT static const GlobalAmbientLight NONE;
};

inline const GlobalAmbientLight GlobalAmbientLight::NONE{glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, true};

}  // namespace epix::pbr
