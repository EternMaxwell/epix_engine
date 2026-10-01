#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/camera.hpp>
#include <epix/color.hpp>
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
 *  Bevy stores a `bevy_color::Color`, and this port stores an `epix::color::Color`
 *  the same way. */
EPIX_EXPORT struct AmbientLight {
    /** @brief The light's colour. Bevy's default is white. */
    ::epix::color::Color color = ::epix::color::Color::WHITE;
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
    ::epix::color::Color color = ::epix::color::Color::WHITE;
    float brightness                = 80.0f;
    bool affects_lightmapped_meshes = true;

    /** @brief No ambient light at all (Bevy `GlobalAmbientLight::NONE`). */
    EPIX_EXPORT static const GlobalAmbientLight NONE;
};

inline const GlobalAmbientLight GlobalAmbientLight::NONE{::epix::color::Color::WHITE, 0.0f, true};

}  // namespace epix::pbr
