#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/color.hpp>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#include <variant>
#endif

namespace epix::camera {
/** @brief RGBA clear color for render targets (Bevy bevy_camera ClearColor). */
EPIX_EXPORT struct ClearColor {
    /** Bevy 0.18's default: `Color::srgb_u8(43, 44, 47)`, the dark gray of the
     *  bevy website's code blocks. Bevy stores the color itself and converts it
     *  where the value is written, which is what the writes below do. */
    ClearColor() noexcept : m_color(color::Color::srgb_u8(43, 44, 47)) {}
    explicit ClearColor(color::Color value) noexcept : m_color(std::move(value)) {}

    /** @brief Bevy's `Deref` and `DerefMut` to the stored color. */
    const color::Color& operator*() const noexcept { return m_color; }
    color::Color& operator*() noexcept { return m_color; }

    /** @brief The linear RGBA a target write uses. Bevy converts the color at
     *  each use site; this names that conversion so the writes read alike. */
    glm::vec4 to_linear_vec4() const { return color::LinearRgba(m_color).vec4(); }

    bool operator==(const ClearColor&) const = default;

   private:
    color::Color m_color;
};

/** @brief Controls how the render target is cleared before rendering (Bevy
 * bevy_camera ClearColorConfig). */
namespace detail {
struct ClearColorConfigDefault {};
struct ClearColorConfigCustom {
    /** The color to clear with. Bevy's `Custom` variant holds a
     *  `bevy_color::Color` (`clear_color.rs:18`), so this is a full color and
     *  not the linear-only `ClearColor` resource. */
    epix::color::Color color;
};
struct ClearColorConfigNone {};
}  // namespace detail

EPIX_EXPORT struct ClearColorConfig
    : std::variant<detail::ClearColorConfigDefault, detail::ClearColorConfigCustom, detail::ClearColorConfigNone> {
    using Default = detail::ClearColorConfigDefault;
    using Custom  = detail::ClearColorConfigCustom;
    using None    = detail::ClearColorConfigNone;

   private:
    using Base = std::variant<Default, Custom, None>;

   public:
    using Base::Base;
    ClearColorConfig() noexcept : Base(Default{}) {}
    /** @brief A color converts into a `Custom` config (Bevy derives
     *  `From<Color> for ClearColorConfig`, `clear_color.rs:11`). */
    ClearColorConfig(const epix::color::Color& value) noexcept : Base(Custom{value}) {}
};

/** @brief Controls when a camera copies the prior target contents back into
 * its MSAA texture (Bevy MsaaWriteback). */
EPIX_EXPORT enum class MsaaWriteback {
    Off,
    Auto,
    Always,
};
}  // namespace epix::camera
