export module epix.glfw.render;

import epix.core;

namespace epix::glfw::render {
/** @brief Plugin that registers GLFW-specific render target (surface)
 * creation for the render pipeline. */
export struct GLFWRenderPlugin {
    void build(core::App& app);
};
}  // namespace glfw::render

namespace epix::glfw {
export using render::GLFWRenderPlugin;
}