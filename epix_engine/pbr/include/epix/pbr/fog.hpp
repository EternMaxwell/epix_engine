#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <epix/color.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <numbers>
#include <type_traits>
#include <variant>
#endif

namespace epix::pbr {

/** @brief The revised Koschmieder contrast threshold Bevy's visibility-based fog
 *  constructors default to (Bevy `FogFalloff::REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD`,
 *  `fog.rs:433`). */
EPIX_EXPORT inline constexpr float REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD = 0.05f;

/** @brief The Koschmieder exponent for a visibility distance and contrast threshold
 *  (Bevy `FogFalloff::koschmieder`, `fog.rs:460`): `-ln(contrast_threshold) /
 *  visibility`.
 *
 *  A larger visibility gives a smaller exponent, which is the point: the fog is meant
 *  to be that much less dense when you can see further. */
EPIX_EXPORT inline float koschmieder(float visibility, float contrast_threshold) {
    return -std::log(contrast_threshold) / visibility;
}

/** @brief The density of an `Exponential` falloff for a desired visibility distance
 *  (Bevy `FogFalloff::from_visibility_contrast`, `fog.rs:318`). */
EPIX_EXPORT inline float fog_exponential_density(float visibility, float contrast_threshold) {
    return koschmieder(visibility, contrast_threshold);
}

/** @brief The density of an `ExponentialSquared` falloff for a desired visibility
 *  distance (Bevy `FogFalloff::from_visibility_contrast_squared`, `fog.rs:335`).
 *
 *  Note the division by the visibility *again*, on top of the one inside
 *  `koschmieder`: the square root is of `-ln(c) / visibility^2`, not of the plain
 *  Koschmieder exponent. Dropping that second division produces a density that is
 *  wrong by a factor of the visibility, which still looks like fog. */
EPIX_EXPORT inline float fog_exponential_squared_density(float visibility, float contrast_threshold) {
    return std::sqrt(koschmieder(visibility, contrast_threshold) / visibility);
}

/** @brief The extinction coefficients of an `Atmospheric` falloff (Bevy
 *  `FogFalloff::from_visibility_contrast_colors`, `fog.rs:413-422`).
 *
 *  The colour is **inverted** before being raised to e - `powf(1 - rgb, e)` - so that
 *  giving a blue extinction colour produces blue results rather than yellow. Read
 *  mechanically, the returned coefficients are the *survival* of each channel rather
 *  than its attenuation: a blue extinction colour gives blue the *smallest*
 *  coefficient, and a pure-blue colour gives it zero. The coefficients are then scaled
 *  by the Koschmieder exponent and by the alpha channel raised to e, which is how the
 *  alpha "modulates" the effect without changing the falloff mode.
 *
 *  Channels are linear RGBA, as Bevy takes them from `LinearRgba`; the conversion from
 *  whatever colour type the caller holds is the caller's. */
EPIX_EXPORT EPIX_API glm::vec3 fog_atmospheric_extinction(glm::vec3 extinction_rgb,
                                                        float extinction_alpha,
                                                        float visibility,
                                                        float contrast_threshold);

/** @brief The in-scattering coefficients of an `Atmospheric` falloff (Bevy
 *  `FogFalloff::from_visibility_contrast_colors`, `fog.rs:424-425`).
 *
 *  Not inverted, unlike the extinction: the in-scattered light is the colour you see,
 *  so a blue in-scattering colour adds blue. */
EPIX_EXPORT inline glm::vec3 fog_atmospheric_inscattering(glm::vec3 inscattering_rgb) {
    const float e = static_cast<float>(std::numbers::e);
    return glm::vec3(std::pow(inscattering_rgb.x, e), std::pow(inscattering_rgb.y, e), std::pow(inscattering_rgb.z, e));
}

/** @brief The linear fog falloff (Bevy `FogFalloff::Linear`). */
EPIX_EXPORT struct FogFalloffLinear {
    float start = 0.0f;
    float end   = 100.0f;
};

/** @brief A linear multiplier on world distance (Bevy `FogFalloff::Exponential`). */
EPIX_EXPORT struct FogFalloffExponential {
    float density = 0.0f;
};

/** @brief A squared exponential falloff, growing more slowly up close (Bevy
 *  `FogFalloff::ExponentialSquared`). */
EPIX_EXPORT struct FogFalloffExponentialSquared {
    float density = 0.0f;
};

/** @brief Extinction and in-scattering coefficients (Bevy `FogFalloff::Atmospheric`).
 *  `extinction` is what fades the scene out and `inscattering` is the colour it fades
 *  towards, which is why one is a survival per channel and the other the colour seen. */
EPIX_EXPORT struct FogFalloffAtmospheric {
    glm::vec3 extinction{0.0f};
    glm::vec3 inscattering{0.0f};
};

/** @brief How fog intensity grows with distance (Bevy `FogFalloff`).
 *
 *  A variant rather than a mode tag, as `Projection` is, so a falloff carries its own
 *  parameters and cannot be read as the wrong one.
 *
 *  The visibility-based constructors exist because non-linear fog is hard to tune by
 *  its coefficients: asking for "visible out to 300 units" is something an artist can
 *  answer, and `koschmieder` turns that into a density. */
EPIX_EXPORT struct FogFalloff {
    std::variant<FogFalloffLinear, FogFalloffExponential, FogFalloffExponentialSquared, FogFalloffAtmospheric> value;

    /** @brief The exponential falloff that makes a scene visible out to `visibility`
     *  world units (Bevy `FogFalloff::from_visibility`). */
    static FogFalloff from_visibility(float visibility) {
        return from_visibility_contrast(visibility, REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD);
    }

    /** @brief The exponential falloff for a visibility distance and contrast
     *  threshold (Bevy `FogFalloff::from_visibility_contrast`). */
    static FogFalloff from_visibility_contrast(float visibility, float contrast_threshold) {
        return FogFalloff{FogFalloffExponential{.density = fog_exponential_density(visibility, contrast_threshold)}};
    }

    /** @brief The squared exponential falloff for a visibility distance (Bevy
     *  `FogFalloff::from_visibility_squared`). */
    static FogFalloff from_visibility_squared(float visibility) {
        return from_visibility_contrast_squared(visibility, REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD);
    }

    /** @brief The squared exponential falloff for a visibility distance and contrast
     *  threshold (Bevy `FogFalloff::from_visibility_contrast_squared`). */
    static FogFalloff from_visibility_contrast_squared(float visibility, float contrast_threshold) {
        return FogFalloff{
            FogFalloffExponentialSquared{.density = fog_exponential_squared_density(visibility, contrast_threshold)}};
    }

    /** @brief An atmospheric falloff using one colour for both extinction and
     *  in-scattering (Bevy `FogFalloff::from_visibility_color`). */
    static FogFalloff from_visibility_color(float visibility, ::epix::color::Color extinction_inscattering_color) {
        return from_visibility_contrast_colors(visibility, REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD,
                                               extinction_inscattering_color, extinction_inscattering_color);
    }

    /** @brief An atmospheric falloff for a visibility distance and separate extinction
     *  and in-scattering colours (Bevy `FogFalloff::from_visibility_colors`). */
    static FogFalloff from_visibility_colors(float visibility,
                                             ::epix::color::Color extinction_color,
                                             ::epix::color::Color inscattering_color) {
        return from_visibility_contrast_colors(visibility, REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD, extinction_color,
                                               inscattering_color);
    }

    /** @brief An atmospheric falloff using one colour for both extinction and in-scattering
     *  (Bevy `FogFalloff::from_visibility_contrast_color`). */
    static FogFalloff from_visibility_contrast_color(float visibility,
                                                     float contrast_threshold,
                                                     ::epix::color::Color extinction_inscattering_color) {
        return from_visibility_contrast_colors(visibility, contrast_threshold, extinction_inscattering_color,
                                               extinction_inscattering_color);
    }

    /** @brief An atmospheric falloff for a visibility distance, contrast threshold and
     *  separate extinction and in-scattering colours (Bevy
     *  `FogFalloff::from_visibility_contrast_colors`). */
    static FogFalloff from_visibility_contrast_colors(float visibility,
                                                      float contrast_threshold,
                                                      ::epix::color::Color extinction_color,
                                                      ::epix::color::Color inscattering_color) {
        // Bevy converts both colours to `LinearRgba` and reads the extinction's alpha
        // separately (`fog.rs:410-427`).
        const ::epix::color::LinearRgba extinction   = extinction_color.linear();
        const ::epix::color::LinearRgba inscattering = inscattering_color.linear();
        return FogFalloff{FogFalloffAtmospheric{
            .extinction   = fog_atmospheric_extinction(glm::vec3(extinction.red, extinction.green, extinction.blue),
                                                       extinction.alpha, visibility, contrast_threshold),
            .inscattering = fog_atmospheric_inscattering(glm::vec3(inscattering.red, inscattering.green,
                                                                   inscattering.blue)),
        }};
    }
};

/** @brief Distance fog on a camera, affecting the meshes it renders (Bevy
 *  `DistanceFog`).
 *
 *  The alpha channel of the colour modulates the effect without changing the falloff,
 *  which is why it is read for the atmospheric extinction above. */
EPIX_EXPORT struct DistanceFog {
    ::epix::color::Color color = ::epix::color::Color::WHITE;
    ::epix::color::Color directional_light_color = ::epix::color::Color::NONE;
    float directional_light_exponent = 8.0f;
    FogFalloff falloff{FogFalloffLinear{.start = 0.0f, .end = 100.0f}};
};

/** @brief The GPU fog modes, which must stay in step with `mesh_view_types.wgsl`
 *  (Bevy `render/fog.rs:36-40`). */
EPIX_EXPORT inline constexpr std::uint32_t GPU_FOG_MODE_OFF                 = 0;
EPIX_EXPORT inline constexpr std::uint32_t GPU_FOG_MODE_LINEAR              = 1;
EPIX_EXPORT inline constexpr std::uint32_t GPU_FOG_MODE_EXPONENTIAL         = 2;
EPIX_EXPORT inline constexpr std::uint32_t GPU_FOG_MODE_EXPONENTIAL_SQUARED = 3;
EPIX_EXPORT inline constexpr std::uint32_t GPU_FOG_MODE_ATMOSPHERIC         = 4;

/** @brief Fog as the shader reads it (Bevy `GpuFog`, `render/fog.rs:18-33`).
 *
 *  `be` and `bi` are allocated differently per mode, and the shader's
 *  `mesh_view_types.wgsl` explains the packing: linear puts the start and end distances
 *  in `be.xy`, both exponential modes put the density in `be.x`, and atmospheric uses
 *  `be` for the extinction coefficients and `bi` for the in-scattering ones. A reader
 *  who assumes one meaning for `be` will misread three of the four modes.
 *
 *  The layout is 64 bytes: two vec4 colours, then `be` and the exponent sharing a
 *  vec4, then `bi` and the mode sharing another. Those last two are why the padding is
 *  explicit - glm's vec3 is 12 bytes and would leave the struct 60. */
EPIX_EXPORT struct GpuFog {
    glm::vec4 base_color{0.0f};
    glm::vec4 directional_light_color{0.0f};
    glm::vec3 be{0.0f};
    float directional_light_exponent = 0.0f;
    glm::vec3 bi{0.0f};
    std::uint32_t mode = GPU_FOG_MODE_OFF;
};
static_assert(sizeof(GpuFog) == 64, "GpuFog must match the shader's layout");
static_assert(offsetof(GpuFog, base_color) == 0);
static_assert(offsetof(GpuFog, directional_light_color) == 16);
static_assert(offsetof(GpuFog, be) == 32);
static_assert(offsetof(GpuFog, directional_light_exponent) == 44);
static_assert(offsetof(GpuFog, bi) == 48);
static_assert(offsetof(GpuFog, mode) == 60);

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief `GpuFog` is uploaded as raw bytes: the struct is already padded to the
 *  shader's 64-byte layout with its offsets asserted, so a derived layout has nothing
 *  to add. This must precede `FogMeta`, which instantiates
 *  `DynamicUniformBuffer<GpuFog>` and so checks the `ShaderWritable` concept there. */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuFog> : RawShaderType<::epix::pbr::GpuFog> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The GPU fog for a camera's fog component (Bevy `prepare_fog`,
 *  `render/fog.rs:64-113`).
 *
 *  A camera with no fog gets mode off and everything zero, which is how "no fog" is
 *  expressed on the GPU rather than by leaving the binding absent. */
EPIX_EXPORT EPIX_API GpuFog gpu_fog_from(const DistanceFog& fog);

/** @brief A view's offset into the fog uniform buffer (Bevy `ViewFogUniformOffset`,
 *  `render/fog.rs:125-127`), read later by the mesh view bind group. */
EPIX_EXPORT struct ViewFogUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief One `GpuFog` per view (Bevy `FogMeta`, `render/fog.rs:44-46`). */
EPIX_EXPORT struct FogMeta {
    render::render_resource::DynamicUniformBuffer<GpuFog> gpu_fogs;
};

/** @brief Writes one `GpuFog` per view and stamps each view with its offset (Bevy
 *  `prepare_fog`, `render/fog.rs:49-120`).
 *
 *  A view without a `DistanceFog` still gets an entry, with the mode off and every
 *  field zero: "no fog" is expressed on the GPU rather than by leaving the binding
 *  absent, which is what lets one bind group serve both kinds of view. The writer's
 *  capacity is the view count, so all the offsets are valid by construction. */
EPIX_EXPORT EPIX_API void prepare_fog(ecs::Commands commands,
                                      ecs::Res<wgpu::Device> render_device,
                                      ecs::Res<wgpu::Queue> render_queue,
                                      ecs::ResMut<FogMeta> fog_meta,
                                      ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const DistanceFog&>>,
                                                 ecs::With<::epix::render::view::ExtractedView>> views);

}  // namespace epix::pbr
