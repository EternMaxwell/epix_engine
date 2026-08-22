#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <glm/glm.hpp>
#endif

namespace epix::camera {
/** @brief RGBA clear color for render targets (Bevy bevy_camera ClearColor). */
EPIX_EXPORT struct ClearColor : public glm::vec4 {
    using glm::vec4::vec4;
    ClearColor(const glm::vec4& v) noexcept : glm::vec4(v) {}
    glm::vec4 to_vec4() const noexcept { return glm::vec4(*this); }
};

/** @brief Controls how the render target is cleared before rendering (Bevy
 * bevy_camera ClearColorConfig). */
EPIX_EXPORT struct ClearColorConfig {
    enum class Type {
        None,    // don't clear
        Global,  // use world's clear color resource
        Default = Global,
        Custom,  // use custom clear color
    } type = Type::Default;
    ClearColor clear_color{0.0f, 0.0f, 0.0f, 1.0f};

    static ClearColorConfig none() noexcept { return ClearColorConfig{Type::None}; }
    static ClearColorConfig def() noexcept { return ClearColorConfig{Type::Default}; }
    static ClearColorConfig global() noexcept { return ClearColorConfig{Type::Global}; }
    static ClearColorConfig custom(const glm::vec4& color) noexcept { return ClearColorConfig{Type::Custom, color}; }
};
}  // namespace epix::camera
