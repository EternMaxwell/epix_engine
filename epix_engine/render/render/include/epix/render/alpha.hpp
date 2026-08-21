#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <variant>
#endif

namespace epix::render {
/**
 * @brief Sets how a material's base color alpha channel is used for transparency.
 *
 * Mirrors `bevy_render::alpha::AlphaMode` (0.18).
 */
EPIX_EXPORT struct AlphaMode {
    enum class Type : std::uint8_t {
        Opaque,
        Mask,
        Blend,
        Premultiplied,
        AlphaToCoverage,
        Add,
        Multiply,
    };

    Type type = Type::Opaque;
    /** @brief Threshold used by `Mask`: below -> fully transparent, at/above -> fully opaque. */
    float mask_threshold = 0.5f;

    constexpr AlphaMode() noexcept = default;
    constexpr AlphaMode(Type t) noexcept : type(t) {}

    /** @brief Base color alpha values are overridden to be fully opaque (1.0). */
    static constexpr AlphaMode opaque() noexcept { return AlphaMode(Type::Opaque); }
    /** @brief Reduce transparency to fully opaque or fully transparent based on a threshold. */
    static constexpr AlphaMode mask(float threshold = 0.5f) noexcept {
        AlphaMode m(Type::Mask);
        m.mask_threshold = threshold;
        return m;
    }
    /** @brief Standard alpha blending; base color alpha defines the opacity. */
    static constexpr AlphaMode blend() noexcept { return AlphaMode(Type::Blend); }
    /** @brief Alpha blending with premultiplied RGB channels. */
    static constexpr AlphaMode premultiplied() noexcept { return AlphaMode(Type::Premultiplied); }
    /** @brief Alpha-to-coverage; requires MSAA, identical to `mask(0.5)` without it. */
    static constexpr AlphaMode alpha_to_coverage() noexcept { return AlphaMode(Type::AlphaToCoverage); }
    /** @brief Additive blending (like light). */
    static constexpr AlphaMode add() noexcept { return AlphaMode(Type::Add); }
    /** @brief Multiplicative blending (like pigments). */
    static constexpr AlphaMode multiply() noexcept { return AlphaMode(Type::Multiply); }

    bool operator==(const AlphaMode&) const = default;
};

}  // namespace epix::render
