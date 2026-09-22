#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <epix/common.hpp>
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
EPIX_EXPORT inline glm::vec3 fog_atmospheric_extinction(glm::vec3 extinction_rgb, float extinction_alpha,
                                                        float visibility, float contrast_threshold) {
    const float e = static_cast<float>(std::numbers::e);
    return glm::vec3(std::pow(1.0f - extinction_rgb.x, e), std::pow(1.0f - extinction_rgb.y, e),
                     std::pow(1.0f - extinction_rgb.z, e)) *
           koschmieder(visibility, contrast_threshold) * std::pow(extinction_alpha, e);
}

/** @brief The in-scattering coefficients of an `Atmospheric` falloff (Bevy
 *  `FogFalloff::from_visibility_contrast_colors`, `fog.rs:424-425`).
 *
 *  Not inverted, unlike the extinction: the in-scattered light is the colour you see,
 *  so a blue in-scattering colour adds blue. */
EPIX_EXPORT inline glm::vec3 fog_atmospheric_inscattering(glm::vec3 inscattering_rgb) {
    const float e = static_cast<float>(std::numbers::e);
    return glm::vec3(std::pow(inscattering_rgb.x, e), std::pow(inscattering_rgb.y, e),
                     std::pow(inscattering_rgb.z, e));
}

}  // namespace epix::pbr
