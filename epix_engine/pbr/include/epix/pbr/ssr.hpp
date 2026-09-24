#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <glm/glm.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The component that enables screen-space reflections for a camera
 *  (Bevy `ScreenSpaceReflections`, `bevy_pbr/src/ssr/mod.rs:85-128`).
 *
 *  The fields mirror Bevy's, including the defaults the plugin's own comment
 *  takes from a public gist. Bevy marks the component `#[require(DepthPrepass,
 *  DeferredPrepass)]` and SSR only appears with deferred rendering; Epix has no
 *  required-components declaration, so a caller adding this component is
 *  responsible for the two prepass components. */
EPIX_EXPORT struct ScreenSpaceReflections {
    /** @brief The maximum PBR roughness level that will enable screen-space
     *  reflections. */
    float perceptual_roughness_threshold = 0.1f;
    /** @brief The constant thickness the depth buffer's fragments are assumed
     *  to be cuboids of, because a depth buffer is only 2.5D information. */
    float thickness = 0.25f;
    /** @brief The number of steps taken at regular intervals to find an initial
     *  intersection. Must not be zero: higher values reflect more reliably at a
     *  higher GPU cost. */
    std::uint32_t linear_steps = 16;
    /** @brief The exponent applied in the linear part of the march. 1.0 gives
     *  equidistant steps; higher values compress the earlier steps and expand
     *  the later ones, so small integers such as 1 or 2 are best. */
    float linear_march_exponent = 1.0f;
    /** @brief The number of bisection steps performed once the linear search
     *  has found an intersection. */
    std::uint32_t bisection_steps = 4;
    /** @brief Whether to approximate the root position with the secant method,
     *  by solving for the line-line intersection between the ray approach rate
     *  and the surface gradient. */
    bool use_secant = true;
};

/** @brief The GPU form of the settings above (Bevy
 *  `ScreenSpaceReflectionsUniform`, `bevy_pbr/src/ssr/mod.rs:130-143`).
 *
 *  Bevy derives `ShaderType` on the same six fields with the boolean widened to
 *  a `u32`, so the uniform is 24 bytes and that is the `min_binding_size` the
 *  mesh view layout declares for binding 15. */
EPIX_EXPORT struct ScreenSpaceReflectionsUniform {
    float perceptual_roughness_threshold = 0.0f;
    float thickness = 0.0f;
    std::uint32_t linear_steps = 0;
    float linear_march_exponent = 0.0f;
    std::uint32_t bisection_steps = 0;
    /** @brief A boolean converted to a `u32`. */
    std::uint32_t use_secant = 0;

    /** @brief The conversion Bevy spells `impl From<ScreenSpaceReflections>`
     *  (`ssr/mod.rs:581-591`), with the boolean widened. */
    static ScreenSpaceReflectionsUniform from(const ScreenSpaceReflections& settings) {
        return ScreenSpaceReflectionsUniform{
            .perceptual_roughness_threshold = settings.perceptual_roughness_threshold,
            .thickness = settings.thickness,
            .linear_steps = settings.linear_steps,
            .linear_march_exponent = settings.linear_march_exponent,
            .bisection_steps = settings.bisection_steps,
            .use_secant = settings.use_secant ? 1u : 0u,
        };
    }
};

// Six four-byte scalars, which is both the object representation and the std140
// layout, so the uniform is 24 bytes.
static_assert(sizeof(ScreenSpaceReflectionsUniform) == 24, "the SSR uniform must match the shader's 24 bytes");

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief The uniform's fields are already laid out the way the shader wants
 *  them (Bevy's `ShaderType` derive on the same struct). */
template <>
struct ShaderTypeInfo<::epix::pbr::ScreenSpaceReflectionsUniform>
    : RawShaderType<::epix::pbr::ScreenSpaceReflectionsUniform> {};
}  // namespace epix::render::render_resource
