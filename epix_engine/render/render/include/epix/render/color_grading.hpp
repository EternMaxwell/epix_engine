#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <utility>
#endif

namespace epix::render::view {
/**
 * @brief Filmic color grading values applied to the image as a whole (Bevy
 * ColorGradingGlobal).
 */
EPIX_EXPORT struct ColorGradingGlobal {
    /** @brief Exposure value (EV) offset, measured in stops. */
    float exposure = 0.0f;
    /** @brief Chromaticity *x* adjustment; positive reddens, negative blues. */
    float temperature = 0.0f;
    /** @brief Chromaticity *y* adjustment; positive magenta, negative green. */
    float tint = 0.0f;
    /** @brief Hue adjustment in radians. */
    float hue = 0.0f;
    /** @brief Saturation adjustment applied after tonemapping; 0 = grayscale. */
    float post_saturation = 1.0f;
    /** @brief Luminance range considered "midtones" (default 0.2..0.7). */
    std::pair<float, float> midtones_range{0.2f, 0.7f};
};

/**
 * @brief A section of color grading values applied to shadows, midtones or
 * highlights (Bevy ColorGradingSection).
 */
EPIX_EXPORT struct ColorGradingSection {
    /** @brief Saturation; below 1 desaturates, 0 = grayscale. */
    float saturation = 1.0f;
    /** @brief Contrast; 1.0 applies no change. */
    float contrast = 1.0f;
    /** @brief Nonlinear luminance adjustment (ASC CDL exponent n). */
    float gamma = 1.0f;
    /** @brief Linear luminance adjustment (ASC CDL slope s). */
    float gain = 1.0f;
    /** @brief Fixed luminance adjustment (ASC CDL offset o). */
    float lift = 0.0f;
};

/**
 * @brief Configures filmic color grading parameters for a camera (Bevy
 * ColorGrading).
 */
EPIX_EXPORT struct ColorGrading {
    /** @brief Grading values applied to the whole image. */
    ColorGradingGlobal global;
    /** @brief Grading values for the darker parts of the image. */
    ColorGradingSection shadows;
    /** @brief Grading values for intermediate brightness. */
    ColorGradingSection midtones;
    /** @brief Grading values for the lighter parts of the image. */
    ColorGradingSection highlights;

    /** @brief Creates grading with identical shadow, midtone, and highlight
     * sections (Bevy ColorGrading::with_identical_sections). */
    static ColorGrading with_identical_sections(ColorGradingGlobal global, ColorGradingSection section) {
        return ColorGrading{std::move(global), section, section, section};
    }
};

/**
 * @brief The ColorGrading structure packed for the GPU (Bevy
 * ColorGradingUniform).
 */
EPIX_EXPORT struct ColorGradingUniform {
    // Layout mirrors Bevy's ShaderType output: WGSL mat3x3 is 48 bytes
    // (16-aligned columns) and every vec3 member occupies a 16-byte slot in
    // uniform address space, so explicit padding keeps the C++ struct
    // byte-identical to the GPU layout (struct size 160, align 16).
    /** @brief Color balance matrix. */
    glm::mat3 balance = glm::mat3(1.0f);
    /** @brief Pad to WGSL mat3x3 size (48). */
    float _pad_balance[3]{};
    /** @brief Per-channel saturation. */
    glm::vec3 saturation = glm::vec3(1.0f);
    float _pad_saturation{};
    /** @brief Per-channel contrast. */
    glm::vec3 contrast = glm::vec3(1.0f);
    float _pad_contrast{};
    /** @brief Per-channel gamma. */
    glm::vec3 gamma = glm::vec3(1.0f);
    float _pad_gamma{};
    /** @brief Per-channel gain. */
    glm::vec3 gain = glm::vec3(1.0f);
    float _pad_gain{};
    /** @brief Per-channel lift. */
    glm::vec3 lift = glm::vec3(0.0f);
    float _pad_lift{};
    /** @brief Midtone luminance range. */
    glm::vec2 midtone_range = glm::vec2(0.2f, 0.7f);
    /** @brief Exposure in stops. */
    float exposure = 0.0f;
    /** @brief Hue adjustment in radians. */
    float hue = 0.0f;
    /** @brief Post-tonemapping saturation. */
    float post_saturation = 1.0f;
    /** @brief Pad to WGSL struct size 160. */
    float _pad_tail[3]{};
};
static_assert(sizeof(ColorGradingUniform) == 160);

/** @brief Packs CPU color-grading controls into Bevy's GPU representation,
 * including its D65/CAM16 white-balance transform. */
inline ColorGradingUniform to_uniform(const ColorGrading& component) {
    // GLM is column-major, matching Bevy/glam's Mat3 constructor.
    const glm::mat3 rgb_to_lms{
        glm::vec3(0.311692f, 0.0905138f, 0.00764433f),
        glm::vec3(0.652085f, 0.901341f, 0.0486554f),
        glm::vec3(0.0362225f, 0.00814478f, 0.943700f),
    };
    const glm::mat3 lms_to_rgb{
        glm::vec3(4.06305f, -0.40791f, -0.0118812f),
        glm::vec3(-2.93241f, 1.40437f, -0.0486532f),
        glm::vec3(-0.130646f, 0.00353630f, 1.0605344f),
    };
    const glm::vec2 xy =
        glm::vec2(0.31272f, 0.32903f) + glm::vec2(-component.global.temperature, component.global.tint);
    const glm::vec3 lms =
        glm::vec3(0.701634f, 1.15856f, -0.904175f) +
        (glm::vec3(-0.051461f, 0.045854f, 0.953127f) + glm::vec3(0.452749f, -0.296122f, -0.955206f) * xy.x) / xy.y;
    const glm::vec3 scale = glm::vec3(0.975538f, 1.01648f, 1.08475f) / lms;
    const glm::mat3 adjustment{
        glm::vec3(scale.x, 0.0f, 0.0f),
        glm::vec3(0.0f, scale.y, 0.0f),
        glm::vec3(0.0f, 0.0f, scale.z),
    };

    ColorGradingUniform uniform;
    uniform.balance = lms_to_rgb * adjustment * rgb_to_lms;
    uniform.saturation =
        glm::vec3(component.shadows.saturation, component.midtones.saturation, component.highlights.saturation);
    uniform.contrast =
        glm::vec3(component.shadows.contrast, component.midtones.contrast, component.highlights.contrast);
    uniform.gamma           = glm::vec3(component.shadows.gamma, component.midtones.gamma, component.highlights.gamma);
    uniform.gain            = glm::vec3(component.shadows.gain, component.midtones.gain, component.highlights.gain);
    uniform.lift            = glm::vec3(component.shadows.lift, component.midtones.lift, component.highlights.lift);
    uniform.midtone_range   = glm::vec2(component.global.midtones_range.first, component.global.midtones_range.second);
    uniform.exposure        = component.global.exposure;
    uniform.hue             = component.global.hue;
    uniform.post_saturation = component.global.post_saturation;
    return uniform;
}

}  // namespace epix::render::view
