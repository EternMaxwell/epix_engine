#pragma once

#ifndef EPIX_CXX_MODULE
#include <cassert>
#include <cstdint>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#include <limits>
#endif

namespace epix::math {

namespace detail {
/** @brief Bevy's `u32::saturating_add_signed`: adds a signed delta, clamping at
 *  zero and at `u32::MAX`. */
constexpr std::uint32_t saturating_add_signed(std::uint32_t value, std::int32_t delta) noexcept {
    const std::int64_t sum = static_cast<std::int64_t>(value) + static_cast<std::int64_t>(delta);
    if (sum < 0) return 0;
    if (sum > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(sum);
}

/** @brief Bevy's `i32::saturating_add_signed`. */
constexpr std::int32_t saturating_add_signed(std::int32_t value, std::int32_t delta) noexcept {
    const std::int64_t sum = static_cast<std::int64_t>(value) + static_cast<std::int64_t>(delta);
    if (sum < std::numeric_limits<std::int32_t>::min()) return std::numeric_limits<std::int32_t>::min();
    if (sum > std::numeric_limits<std::int32_t>::max()) return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(sum);
}
}  // namespace detail

EPIX_EXPORT struct IRect;
EPIX_EXPORT struct URect;

/** @brief A rectangle defined by two opposite corners, in floats (Bevy `Rect`,
 *  `bevy_math/src/rects/rect.rs:28`).
 *
 *  Axis aligned, with `min`/`max` stored directly: as in Bevy, keeping the
 *  min/max invariant is the caller's business when assigning the fields. */
EPIX_EXPORT struct Rect {
    /** @brief The minimum corner point. */
    glm::vec2 min{};
    /** @brief The maximum corner point. */
    glm::vec2 max{};

    /** @brief An empty rect of infinitely negative size, so a union with it
     *  returns the other rect (Bevy `Rect::EMPTY`). */
    static const Rect EMPTY;

    /** @brief A rect from four corner coordinates in any order (Bevy
     *  `Rect::new`, which C++ reserves as a keyword). */
    static Rect create(float x0, float y0, float x1, float y1) noexcept {
        return from_corners(glm::vec2(x0, y0), glm::vec2(x1, y1));
    }
    /** @brief A rect from two opposite corners in any order (Bevy `from_corners`). */
    static Rect from_corners(glm::vec2 p0, glm::vec2 p1) noexcept {
        return Rect{.min = glm::min(p0, p1), .max = glm::max(p0, p1)};
    }
    /** @brief A rect from its center and size, which must not be negative (Bevy
     *  `from_center_size`). */
    static Rect from_center_size(glm::vec2 origin, glm::vec2 size) noexcept {
        assert(glm::all(glm::greaterThanEqual(size, glm::vec2(0.0f))) && "Rect size must be positive");
        return from_center_half_size(origin, size / 2.0f);
    }
    /** @brief A rect from its center and half-size, which must not be negative
     *  (Bevy `from_center_half_size`). */
    static Rect from_center_half_size(glm::vec2 origin, glm::vec2 half_size) noexcept {
        assert(glm::all(glm::greaterThanEqual(half_size, glm::vec2(0.0f))) && "Rect half_size must be positive");
        return Rect{.min = origin - half_size, .max = origin + half_size};
    }

    /** @brief Whether the rect is empty (Bevy `Rect::is_empty`). */
    bool is_empty() const noexcept { return glm::any(glm::greaterThanEqual(min, max)); }
    /** @brief `max.x - min.x`. */
    float width() const noexcept { return max.x - min.x; }
    /** @brief `max.y - min.y`. */
    float height() const noexcept { return max.y - min.y; }
    /** @brief The size. */
    glm::vec2 size() const noexcept { return max - min; }
    /** @brief The half-size. */
    glm::vec2 half_size() const noexcept { return size() * 0.5f; }
    /** @brief The center point. */
    glm::vec2 center() const noexcept { return (min + max) * 0.5f; }
    /** @brief Whether a point lies inside, edges included (Bevy `contains`). */
    bool contains(glm::vec2 point) const noexcept {
        return glm::all(glm::greaterThanEqual(point, min) & glm::lessThanEqual(point, max));
    }
    /** @brief The smallest rect enclosing this one and @p other (Bevy `union`). */
    Rect union_with(Rect other) const noexcept {
        return Rect{.min = glm::min(min, other.min), .max = glm::max(max, other.max)};
    }
    /** @brief The smallest rect enclosing this one and a point (Bevy `union_point`). */
    Rect union_point(glm::vec2 other) const noexcept {
        return Rect{.min = glm::min(min, other), .max = glm::max(max, other)};
    }
    /** @brief The largest rect enclosed in both (Bevy `intersect`).
     *
     *  A collapsed min enforces the invariant, so `width()`/`height()` never go
     *  negative, as in Bevy. */
    Rect intersect(Rect other) const noexcept {
        Rect result{.min = glm::max(min, other.min), .max = glm::min(max, other.max)};
        result.min = glm::min(result.min, result.max);
        return result;
    }
    /** @brief The rect expanded evenly on all sides, a negative expansion
     *  shrinking it (Bevy `inflate`). */
    Rect inflate(float expansion) const noexcept {
        Rect result{.min = min - expansion, .max = max + expansion};
        result.min = glm::min(result.min, result.max);
        return result;
    }
    /** @brief This rect's coordinates relative to @p other in a normalized
     *  `[0..1] x [0..1]` system (Bevy `normalize`). */
    Rect normalize(Rect other) const noexcept {
        const glm::vec2 outer_size = other.size();
        return Rect{.min = (min - other.min) / outer_size, .max = (max - other.min) / outer_size};
    }
    /** @brief The area. */
    float area() const noexcept { return width() * height(); }

    /** @brief This rect as `IRect` (Bevy `Rect::as_irect`). */
    IRect as_irect() const noexcept;
    /** @brief This rect as `URect` (Bevy `Rect::as_urect`). */
    URect as_urect() const noexcept;
};

/** @brief A rectangle defined by two opposite corners, in signed integers (Bevy
 *  `IRect`, `bevy_math/src/rects/irect.rs:28`). */
EPIX_EXPORT struct IRect {
    /** @brief The minimum corner point. */
    glm::ivec2 min{};
    /** @brief The maximum corner point. */
    glm::ivec2 max{};

    /** @brief An empty rect of extremely negative size (Bevy `IRect::EMPTY`). */
    static const IRect EMPTY;

    /** @brief A rect from four corner coordinates in any order (Bevy
     *  `IRect::new`, which C++ reserves as a keyword). */
    static IRect create(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) noexcept {
        return from_corners(glm::ivec2(x0, y0), glm::ivec2(x1, y1));
    }
    /** @brief A rect from two opposite corners in any order (Bevy `from_corners`). */
    static IRect from_corners(glm::ivec2 p0, glm::ivec2 p1) noexcept {
        return IRect{.min = glm::min(p0, p1), .max = glm::max(p0, p1)};
    }
    /** @brief A rect from its center and size (Bevy `IRect::from_center_size`).
     *
     *  Bevy checks the size with `debug_assert!`, which this port's release
     *  builds do not compile in either. */
    static IRect from_center_size(glm::ivec2 origin, glm::ivec2 size) noexcept {
        assert(glm::all(glm::greaterThanEqual(size, glm::ivec2(0))) && "IRect size must be positive");
        return from_center_half_size(origin, size / 2);
    }
    /** @brief A rect from its center and half-size (Bevy `from_center_half_size`). */
    static IRect from_center_half_size(glm::ivec2 origin, glm::ivec2 half_size) noexcept {
        assert(glm::all(glm::greaterThanEqual(half_size, glm::ivec2(0))) && "IRect half_size must be positive");
        return IRect{.min = origin - half_size, .max = origin + half_size};
    }

    /** @brief Whether the rect is empty (Bevy `IRect::is_empty`). */
    bool is_empty() const noexcept { return glm::any(glm::greaterThanEqual(min, max)); }
    /** @brief `max.x - min.x`. */
    std::int32_t width() const noexcept { return max.x - min.x; }
    /** @brief `max.y - min.y`. */
    std::int32_t height() const noexcept { return max.y - min.y; }
    /** @brief The size. */
    glm::ivec2 size() const noexcept { return max - min; }
    /** @brief The half-size, rounded down for an odd size as Bevy's integer
     *  division is. */
    glm::ivec2 half_size() const noexcept { return size() / 2; }
    /** @brief The center, rounded down for an odd sum (Bevy `center`). */
    glm::ivec2 center() const noexcept { return (min + max) / 2; }
    /** @brief Whether a point lies inside, edges included (Bevy `contains`). */
    bool contains(glm::ivec2 point) const noexcept {
        return glm::all(glm::greaterThanEqual(point, min) & glm::lessThanEqual(point, max));
    }
    /** @brief The smallest rect enclosing this one and @p other (Bevy `union`). */
    IRect union_with(IRect other) const noexcept {
        return IRect{.min = glm::min(min, other.min), .max = glm::max(max, other.max)};
    }
    /** @brief The smallest rect enclosing this one and a point (Bevy `union_point`). */
    IRect union_point(glm::ivec2 other) const noexcept {
        return IRect{.min = glm::min(min, other), .max = glm::max(max, other)};
    }
    /** @brief The largest rect enclosed in both (Bevy `intersect`). */
    IRect intersect(IRect other) const noexcept {
        IRect result{.min = glm::max(min, other.min), .max = glm::min(max, other.max)};
        result.min = glm::min(result.min, result.max);
        return result;
    }
    /** @brief The rect expanded evenly on all sides, saturating (Bevy `inflate`). */
    IRect inflate(std::int32_t expansion) const noexcept {
        IRect result{.min = glm::ivec2(detail::saturating_add_signed(min.x, -expansion),
                                       detail::saturating_add_signed(min.y, -expansion)),
                     .max = glm::ivec2(detail::saturating_add_signed(max.x, expansion),
                                       detail::saturating_add_signed(max.y, expansion))};
        result.min = glm::min(result.min, result.max);
        return result;
    }

    /** @brief This rect as `Rect` (Bevy `IRect::as_rect`). */
    Rect as_rect() const noexcept { return Rect::from_corners(glm::vec2(min), glm::vec2(max)); }
    /** @brief This rect as `URect` (Bevy `IRect::as_urect`). */
    URect as_urect() const noexcept;
};

/** @brief A rectangle defined by two opposite corners, in unsigned integers (Bevy
 *  `URect`, `bevy_math/src/rects/urect.rs:28`). */
EPIX_EXPORT struct URect {
    /** @brief The minimum corner point. */
    glm::uvec2 min{};
    /** @brief The maximum corner point. */
    glm::uvec2 max{};

    /** @brief An empty rect of extremely large negative size (Bevy `URect::EMPTY`). */
    static const URect EMPTY;

    /** @brief A rect from four corner coordinates in any order (Bevy
     *  `URect::new`, which C++ reserves as a keyword). */
    static URect create(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) noexcept {
        return from_corners(glm::uvec2(x0, y0), glm::uvec2(x1, y1));
    }
    /** @brief A rect from two opposite corners in any order (Bevy `from_corners`). */
    static URect from_corners(glm::uvec2 p0, glm::uvec2 p1) noexcept {
        return URect{.min = glm::min(p0, p1), .max = glm::max(p0, p1)};
    }
    /** @brief A rect from its center and size (Bevy `URect::from_center_size`).
     *
     *  Bevy's panic text interpolates both vectors; the wording is kept. */
    static URect from_center_size(glm::uvec2 origin, glm::uvec2 size) noexcept {
        assert(glm::all(glm::greaterThanEqual(origin, size / 2u)) &&
               "Origin must always be greater than or equal to (size / 2) otherwise the rectangle is undefined!");
        return from_center_half_size(origin, size / 2u);
    }
    /** @brief A rect from its center and half-size (Bevy `from_center_half_size`). */
    static URect from_center_half_size(glm::uvec2 origin, glm::uvec2 half_size) noexcept {
        assert(glm::all(glm::greaterThanEqual(origin, half_size)) &&
               "Origin must always be greater than or equal to half_size otherwise the rectangle is undefined!");
        return URect{.min = origin - half_size, .max = origin + half_size};
    }

    /** @brief Whether the rect is empty (Bevy `URect::is_empty`). */
    bool is_empty() const noexcept { return glm::any(glm::greaterThanEqual(min, max)); }
    /** @brief `max.x - min.x`. */
    constexpr std::uint32_t width() const noexcept { return max.x - min.x; }
    /** @brief `max.y - min.y`. */
    constexpr std::uint32_t height() const noexcept { return max.y - min.y; }
    /** @brief The size. */
    glm::uvec2 size() const noexcept { return max - min; }
    /** @brief The half-size, rounded down for an odd size as Bevy's integer
     *  division is. */
    glm::uvec2 half_size() const noexcept { return size() / 2u; }
    /** @brief The center, rounded down for an odd sum (Bevy `center`). */
    glm::uvec2 center() const noexcept { return (min + max) / 2u; }
    /** @brief Whether a point lies inside, edges included (Bevy `contains`). */
    bool contains(glm::uvec2 point) const noexcept {
        return glm::all(glm::greaterThanEqual(point, min) & glm::lessThanEqual(point, max));
    }
    /** @brief The smallest rect enclosing this one and @p other (Bevy `union`). */
    URect union_with(URect other) const noexcept {
        return URect{.min = glm::min(min, other.min), .max = glm::max(max, other.max)};
    }
    /** @brief The smallest rect enclosing this one and a point (Bevy `union_point`). */
    URect union_point(glm::uvec2 other) const noexcept {
        return URect{.min = glm::min(min, other), .max = glm::max(max, other)};
    }
    /** @brief The largest rect enclosed in both (Bevy `intersect`). */
    URect intersect(URect other) const noexcept {
        URect result{.min = glm::max(min, other.min), .max = glm::min(max, other.max)};
        result.min = glm::min(result.min, result.max);
        return result;
    }
    /** @brief The rect expanded evenly on all sides, saturating (Bevy `inflate`). */
    URect inflate(std::int32_t expansion) const noexcept {
        URect result{.min = glm::uvec2(detail::saturating_add_signed(min.x, -expansion),
                                       detail::saturating_add_signed(min.y, -expansion)),
                     .max = glm::uvec2(detail::saturating_add_signed(max.x, expansion),
                                       detail::saturating_add_signed(max.y, expansion))};
        result.min = glm::min(result.min, result.max);
        return result;
    }

    /** @brief This rect as `Rect` (Bevy `URect::as_rect`). */
    Rect as_rect() const noexcept { return Rect::from_corners(glm::vec2(min), glm::vec2(max)); }
    /** @brief This rect as `IRect` (Bevy `URect::as_irect`). */
    IRect as_irect() const noexcept { return IRect::from_corners(glm::ivec2(min), glm::ivec2(max)); }
};

// The three empty rects, and the conversions between types that could only be
// defined once every type above was complete.

inline const Rect Rect::EMPTY{glm::vec2(std::numeric_limits<float>::infinity()),
                              glm::vec2(-std::numeric_limits<float>::infinity())};
inline const IRect IRect::EMPTY{glm::ivec2(std::numeric_limits<std::int32_t>::max()),
                                glm::ivec2(std::numeric_limits<std::int32_t>::min())};
inline const URect URect::EMPTY{glm::uvec2(std::numeric_limits<std::uint32_t>::max()), glm::uvec2(0u)};

inline IRect Rect::as_irect() const noexcept { return IRect::from_corners(glm::ivec2(min), glm::ivec2(max)); }
inline URect Rect::as_urect() const noexcept { return URect::from_corners(glm::uvec2(min), glm::uvec2(max)); }
inline URect IRect::as_urect() const noexcept { return URect::from_corners(glm::uvec2(min), glm::uvec2(max)); }

}  // namespace epix::math
