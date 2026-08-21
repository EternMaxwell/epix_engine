#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <utility>
#include <glm/glm.hpp>
#include <epix/ecs.hpp>
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

}  // namespace epix::render::view
