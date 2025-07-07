#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "graph.h"

namespace epix::render::camera {
struct Viewport {
    glm::uvec2 pos;
    glm::uvec2 size;
    glm::vec2 depth_range;
};
struct Camera {
    /// @brief The camera's viewport within the render target.
    std::optional<Viewport> viewport;
    /// @brief Cameras with higher order are rendered on top of cameras with lower order.
    ptrdiff_t order = 0;
    bool active = true;
};
};  // namespace epix::render::camera