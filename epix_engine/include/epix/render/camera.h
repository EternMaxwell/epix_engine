#pragma once

#include <epix/image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "graph.h"

namespace epix::render::camera {
struct Viewport {
    glm::uvec2 pos;
    glm::uvec2 size;
    std::pair<float, float> depth_range{0.0f, 1.0f};
};
struct WindowRef {
    bool primary = true;
    Entity window_entity;  // Used if primary is false
};
struct RenderTarget : std::variant<nvrhi::TextureHandle, WindowRef> {
    using std::variant<nvrhi::TextureHandle, WindowRef>::variant;
    static RenderTarget from_texture(nvrhi::TextureHandle texture) {
        return RenderTarget(texture);
    }
    static RenderTarget from_primary() { return RenderTarget(WindowRef{true}); }
    static RenderTarget from_window(Entity window_entity) {
        return RenderTarget(WindowRef{false, window_entity});
    }
};
struct ComputedCameraValues {
    glm::mat4 projection;
    glm::ivec2 target_size;
    std::optional<glm::ivec2> old_viewport_size;
};
struct ClearColorConfig {
    enum class Type {
        None,     // don't clear
        Default,  // use world's clear color resource
        Custom,   // use custom clear color
    } type = Type::Default;
    glm::vec4 clear_color{0.0f, 0.0f, 0.0f, 1.0f};

    static ClearColorConfig none() {
        return ClearColorConfig{Type::None, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)};
    }
    static ClearColorConfig def() {
        return ClearColorConfig{Type::Default,
                                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)};
    }
    static ClearColorConfig custom(const glm::vec4& color) {
        return ClearColorConfig{Type::Custom, color};
    }
};
struct Camera {
    /// @brief The camera's viewport within the render target.
    std::optional<Viewport> viewport;
    /// @brief Cameras with higher order are rendered on top of cameras with
    /// lower order.
    ptrdiff_t order = 0;
    bool active     = true;

    RenderTarget render_target = RenderTarget::from_primary();
    ComputedCameraValues computed;
    ClearColorConfig clear_color = ClearColorConfig::def();
};
};  // namespace epix::render::camera