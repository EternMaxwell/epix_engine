#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/image.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <utility>
#include <variant>
#include <vector>
#endif

EPIX_EXPORT namespace epix::sprite {

/** @brief Insets defining the four slicing lines of a nine-sliced sprite. */
struct BorderRect {
    glm::vec2 min_inset{0.0f};
    glm::vec2 max_inset{0.0f};

    static constexpr BorderRect zero() noexcept { return all(0.0f); }
    static constexpr BorderRect all(float inset) noexcept {
        return BorderRect{glm::vec2(inset), glm::vec2(inset)};
    }
    static constexpr BorderRect axes(float horizontal, float vertical) noexcept {
        const glm::vec2 insets(horizontal, vertical);
        return BorderRect{insets, insets};
    }

    constexpr BorderRect operator+(const BorderRect& rhs) const noexcept {
        return BorderRect{min_inset + rhs.min_inset, max_inset + rhs.max_inset};
    }
    constexpr BorderRect operator-(const BorderRect& rhs) const noexcept {
        return BorderRect{min_inset - rhs.min_inset, max_inset - rhs.max_inset};
    }
    constexpr BorderRect operator*(float rhs) const noexcept {
        return BorderRect{min_inset * rhs, max_inset * rhs};
    }
    constexpr BorderRect operator/(float rhs) const noexcept {
        return BorderRect{min_inset / rhs, max_inset / rhs};
    }
    bool operator==(const BorderRect&) const = default;
};

/** @brief One texture rectangle drawn into a sized, offset region. */
struct TextureSlice {
    image::Rect texture_rect;
    glm::vec2 draw_size{0.0f};
    glm::vec2 offset{0.0f};

    std::vector<TextureSlice> tiled(float stretch_value, std::pair<bool, bool> tile_axes) const;
    bool operator==(const TextureSlice&) const = default;
};

struct SliceScaleModeStretch {
    bool operator==(const SliceScaleModeStretch&) const = default;
};
struct SliceScaleModeTile {
    float stretch_value = 1.0f;
    bool operator==(const SliceScaleModeTile&) const = default;
};

/** @brief Stretch or tile behavior for a texture slice. */
struct SliceScaleMode : std::variant<SliceScaleModeStretch, SliceScaleModeTile> {
    using Stretch = SliceScaleModeStretch;
    using Tile    = SliceScaleModeTile;
    using Base    = std::variant<Stretch, Tile>;
    using Base::Base;

    SliceScaleMode() : Base(Stretch{}) {}
};

/** @brief Bevy-compatible nine-slice configuration and CPU slicing
 * algorithm. */
struct TextureSlicer {
    BorderRect border{};
    SliceScaleMode center_scale_mode{};
    SliceScaleMode sides_scale_mode{};
    float max_corner_scale = 1.0f;

    std::vector<TextureSlice> compute_slices(image::Rect rect,
                                             std::optional<glm::vec2> render_size = std::nullopt) const;
    bool operator==(const TextureSlicer&) const = default;
};

}  // namespace epix::sprite
