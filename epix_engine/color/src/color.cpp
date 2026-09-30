#include <epix/color/color.hpp>
#include <cmath>
#include <expected>
#include <type_traits>
#include <variant>

namespace epix::color {
const Color Color::WHITE = Color::linear_rgb(1.0f, 1.0f, 1.0f);
const Color Color::BLACK = Color::linear_rgb(0.0f, 0.0f, 0.0f);
const Color Color::NONE = Color::linear_rgba(0.0f, 0.0f, 0.0f, 0.0f);

Color Color::with_alpha(float alpha) const {
    // Bevy copies `self` and rewrites the active variant in place (`color.rs:481-498`), which is
    // what the mutable visit does here: the stored color space is preserved.
    Color result = *this;
    std::visit([alpha](auto& value) { value = value.with_alpha(alpha); }, result.color);
    return result;
}

float Color::alpha() const {
    return std::visit([](const auto& value) -> float { return value.get_alpha(); }, color);
}

float Color::luminance() const {
    return std::visit(
        [](const auto& value) -> float {
            using Space = std::decay_t<decltype(value)>;
            // Bevy converts HSV and HWB to the chosen space, because neither has a luminance of
            // its own (`color.rs:710-711`).
            if constexpr (std::is_same_v<Space, Hsva> || std::is_same_v<Space, Hwba>) {
                return ChosenColorSpace(value).luminance();
            } else {
                return value.luminance();
            }
        },
        color);
}

Color Color::with_luminance(float value) const {
    Color result = *this;
    std::visit(
        [value](auto& space) {
            using Space = std::decay_t<decltype(space)>;
            if constexpr (std::is_same_v<Space, Hsva> || std::is_same_v<Space, Hwba>) {
                // Bevy's `ChosenColorSpace::from(*x).with_luminance(value).into()`
                // (`color.rs:727-728`): the port reads `ChosenColorSpace(*x)` and the trailing
                // `.into()` as `Space(...)`.
                space = Space(ChosenColorSpace(space).with_luminance(value));
            } else {
                space = space.with_luminance(value);
            }
        },
        result.color);
    return result;
}

Color Color::darker(float amount) const {
    Color result = *this;
    std::visit(
        [amount](auto& space) {
            using Space = std::decay_t<decltype(space)>;
            if constexpr (std::is_same_v<Space, Hsva> || std::is_same_v<Space, Hwba>) {
                space = Space(ChosenColorSpace(space).darker(amount));
            } else {
                space = space.darker(amount);
            }
        },
        result.color);
    return result;
}

Color Color::lighter(float amount) const {
    Color result = *this;
    std::visit(
        [amount](auto& space) {
            using Space = std::decay_t<decltype(space)>;
            if constexpr (std::is_same_v<Space, Hsva> || std::is_same_v<Space, Hwba>) {
                space = Space(ChosenColorSpace(space).lighter(amount));
            } else {
                space = space.lighter(amount);
            }
        },
        result.color);
    return result;
}

Color Color::with_hue(float hue) const {
    Color result = *this;
    std::visit(
        [hue](auto& space) {
            using Space = std::decay_t<decltype(space)>;
            // HSL, HSV, HWB, LCH and Oklch define hue natively (`color.rs:785-791`).
            if constexpr (std::is_same_v<Space, Hsla> || std::is_same_v<Space, Hsva> ||
                          std::is_same_v<Space, Hwba> || std::is_same_v<Space, Lcha> ||
                          std::is_same_v<Space, Oklcha>) {
                space = space.with_hue(hue);
            } else {
                space = Space(ChosenColorSpace(space).with_hue(hue));
            }
        },
        result.color);
    return result;
}

float Color::hue() const {
    return std::visit(
        [](const auto& value) -> float {
            using Space = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Space, Hsla> || std::is_same_v<Space, Hsva> ||
                          std::is_same_v<Space, Hwba> || std::is_same_v<Space, Lcha> ||
                          std::is_same_v<Space, Oklcha>) {
                return value.get_hue();
            } else {
                return ChosenColorSpace(value).get_hue();
            }
        },
        color);
}

Color Color::with_saturation(float saturation) const {
    Color result = *this;
    std::visit(
        [saturation](auto& space) {
            using Space = std::decay_t<decltype(space)>;
            // Only HSL and HSV have native saturation; Bevy performs the operation in HSL for
            // every other space (`color.rs:823-832`).
            if constexpr (std::is_same_v<Space, Hsla> || std::is_same_v<Space, Hsva>) {
                space = space.with_saturation(saturation);
            } else {
                space = Space(Hsla(space).with_saturation(saturation));
            }
        },
        result.color);
    return result;
}

float Color::saturation() const {
    return std::visit(
        [](const auto& value) -> float {
            using Space = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Space, Hsla> || std::is_same_v<Space, Hsva>) {
                return value.get_saturation();
            } else {
                return Hsla(value).get_saturation();
            }
        },
        color);
}

Color Color::mix(const Color& other, float factor) const {
    Color result = *this;
    std::visit(
        [&other, factor](auto& space) {
            using Space = std::decay_t<decltype(space)>;
            // Bevy mixes in the receiver's own space, converting `other` into it first
            // (`color.rs:860-871`): `x.mix(&(*other).into(), factor)`.
            space = space.mix(Space(other), factor);
        },
        result.color);
    return result;
}

float Color::distance_squared(const Color& other) const {
    return std::visit(
        [&other](const auto& value) -> float {
            using Space = std::decay_t<decltype(value)>;
            // sRGB, linear RGB, Oklab and Oklch compare in their own space; every other space
            // compares in the chosen space (`color.rs:880-889`).
            if constexpr (std::is_same_v<Space, Srgba> || std::is_same_v<Space, LinearRgba> ||
                          std::is_same_v<Space, Oklaba> || std::is_same_v<Space, Oklcha>) {
                return value.distance_squared(Space(other));
            } else {
                return ChosenColorSpace(value).distance_squared(ChosenColorSpace(other));
            }
        },
        color);
}

std::expected<Color, MismatchedUnitsError> Color::try_interpolate_stable(const Color& other,
                                                                        float t) const {
    // Bevy matches on the *pair* of variants (`color.rs:898-910`): the ten same-space arms mix in
    // that space and every other arm is `Err(MismatchedUnitsError)`. Comparing the variant indices
    // is that match, and a same-space arm's body is the receiver's own `mix`, which converts the
    // other color into the receiver's space - an identity conversion when the spaces already
    // match.
    if (color.index() != other.color.index()) {
        return std::unexpected(MismatchedUnitsError{});
    }
    return mix(other, t);
}

}  // namespace epix::color
