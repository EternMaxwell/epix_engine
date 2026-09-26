#pragma once

#ifndef EPIX_CXX_MODULE
#include <cassert>
#include <cmath>
#include <epix/common.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <limits>
#include <string_view>
#include <utility>
#endif

namespace epix::mesh {

/** @brief Why a direction vector could not be normalized (Bevy
 *  `InvalidDirectionError`, `bevy_math/src/direction.rs:29-41`).
 *
 *  Bevy derives `thiserror::Error`, so each variant carries display text that
 *  `to_string` returns. */
EPIX_EXPORT enum class InvalidDirectionError {
    /** @brief The length is zero or very close to zero. */
    Zero,
    /** @brief The length is infinite. */
    Infinite,
    /** @brief The length is `NaN`. */
    NaN,
};

/** @brief Return Bevy's display text for an invalid direction (Bevy's
 *  `thiserror` messages on `InvalidDirectionError`). */
EPIX_EXPORT constexpr std::string_view to_string(InvalidDirectionError error) noexcept {
    switch (error) {
        case InvalidDirectionError::Zero:
            return "The length of the direction vector is zero or very close to zero";
        case InvalidDirectionError::Infinite:
            return "The length of the direction vector is `std::f32::INFINITY`";
        case InvalidDirectionError::NaN:
            return "The length of the direction vector is `NaN`";
    }
    return "The length of the direction vector is invalid";
}

/** @brief Classify the length of a vector that cannot be a direction (Bevy
 *  `InvalidDirectionError::from_length`, `direction.rs:35-45`).
 *
 *  A length that is neither NaN nor infinite is zero: those are the only three
 *  ways `Dir3::make` can fail, and the check order is Bevy's. */
EPIX_EXPORT inline InvalidDirectionError invalid_direction_from_length(float length) noexcept {
    if (std::isnan(length)) return InvalidDirectionError::NaN;
    if (!std::isfinite(length)) return InvalidDirectionError::Infinite;
    return InvalidDirectionError::Zero;
}

/** @brief A direction: a non-zero, finite vector held normalized (Bevy `Dir3`,
 *  `bevy_math/src/direction.rs:380-434`).
 *
 *  Bevy wraps a `Vec3` with a private field and derefs to it; Epix keeps the
 *  port's accessor style instead, so `value()` is where `*direction` lands. Only
 *  the construction and the six axis constants are ported: the rest of
 *  `bevy_math::direction.rs` - the rotations, the projections, the sampling
 *  helpers and the `Dir2` counterpart - is still to come. */
EPIX_EXPORT class Dir3 {
   public:
    /** @brief A unit vector pointing along the positive X axis. */
    static const Dir3 X;
    /** @brief A unit vector pointing along the positive Y axis. */
    static const Dir3 Y;
    /** @brief A unit vector pointing along the positive Z axis. */
    static const Dir3 Z;
    /** @brief A unit vector pointing along the negative X axis. */
    static const Dir3 NEG_X;
    /** @brief A unit vector pointing along the negative Y axis. */
    static const Dir3 NEG_Y;
    /** @brief A unit vector pointing along the negative Z axis. */
    static const Dir3 NEG_Z;

    /** @brief Normalize a vector (Bevy `Dir3::new`).
     *
     *  Fails when the vector is zero, infinite or `NaN` - the three cases
     *  `invalid_direction_from_length` names. */
    static std::expected<Dir3, InvalidDirectionError> make(glm::vec3 value) {
        const auto result = make_and_length(value);
        if (!result.has_value()) return std::unexpected(result.error());
        return result->first;
    }

    /** @brief Normalize a vector and report the length it had (Bevy
     *  `Dir3::new_and_length`). */
    static std::expected<std::pair<Dir3, float>, InvalidDirectionError> make_and_length(glm::vec3 value) {
        const float length = glm::length(value);
        if (length > 0.0f && std::isfinite(length)) return std::pair{Dir3{value / length}, length};
        return std::unexpected(invalid_direction_from_length(length));
    }

    /** @brief Wrap a vector that is already normalized (Bevy
     *  `Dir3::new_unchecked`).
     *
     *  Bevy asserts normalization in debug builds only, which is the `assert`
     *  here: it is compiled out in this preset, exactly as Bevy's is in release. */
    static Dir3 make_unchecked(glm::vec3 value) noexcept {
        assert(std::abs(1.0f - glm::length_squared(value)) <= 2.0f * std::numeric_limits<float>::epsilon());
        return Dir3{value};
    }

    /** @brief Normalize a direction given as three components (Bevy
     *  `Dir3::from_xyz`). */
    static std::expected<Dir3, InvalidDirectionError> from_xyz(float x, float y, float z) {
        return make(glm::vec3{x, y, z});
    }

    /** @brief The normalized direction (Bevy's `Deref<Target = Vec3>`). */
    constexpr const glm::vec3& value() const noexcept { return m_value; }

    constexpr bool operator==(const Dir3&) const noexcept = default;

   private:
    constexpr explicit Dir3(glm::vec3 value) noexcept : m_value(value) {}
    glm::vec3 m_value;
};

// The axis constants stay in the header as `inline const`: both images hold an
// identical copy, which is harmless for a constant, and no image imports them.
inline const Dir3 Dir3::X{glm::vec3(1.0f, 0.0f, 0.0f)};
inline const Dir3 Dir3::Y{glm::vec3(0.0f, 1.0f, 0.0f)};
inline const Dir3 Dir3::Z{glm::vec3(0.0f, 0.0f, 1.0f)};
inline const Dir3 Dir3::NEG_X{glm::vec3(-1.0f, 0.0f, 0.0f)};
inline const Dir3 Dir3::NEG_Y{glm::vec3(0.0f, -1.0f, 0.0f)};
inline const Dir3 Dir3::NEG_Z{glm::vec3(0.0f, 0.0f, -1.0f)};

}  // namespace epix::mesh
