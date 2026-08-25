#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <glm/glm.hpp>
#include <variant>
#endif

namespace epix::camera {
/** @brief RGBA clear color for render targets (Bevy bevy_camera ClearColor). */
EPIX_EXPORT struct ClearColor : public glm::vec4 {
    /** Bevy 0.18's default: `Color::srgb_u8(43, 44, 47)`, represented in
     * linear space for GPU rendering. */
    ClearColor() noexcept : glm::vec4(0.02415763f, 0.02518686f, 0.02842604f, 1.0f) {}
    using glm::vec4::vec4;
    ClearColor(const glm::vec4& v) noexcept : glm::vec4(v) {}
    glm::vec4 to_vec4() const noexcept { return glm::vec4(*this); }
};

/** @brief Controls how the render target is cleared before rendering (Bevy
 * bevy_camera ClearColorConfig). */
namespace detail {
struct ClearColorConfigDefault {};
struct ClearColorConfigCustom {
    ClearColor color;
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
};

/** @brief Controls when a camera copies the prior target contents back into
 * its MSAA texture (Bevy MsaaWriteback). */
EPIX_EXPORT enum class MsaaWriteback {
    Off,
    Auto,
    Always,
};
}  // namespace epix::camera
