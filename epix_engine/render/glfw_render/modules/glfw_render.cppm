module;

#include <GLFW/glfw3.h>

export module epix.glfw.render;

import epix.core;
import webgpu;

namespace epix::glfw::render {
export wgpu::Surface get_wgpu_surface(const wgpu::Instance& instance, GLFWwindow* window);

/** @brief Plugin that registers GLFW-specific render target (surface)
 * creation for the render pipeline. */
export struct GLFWRenderPlugin {
    void build(core::App& app);
};
}  // namespace epix::glfw::render

namespace epix::glfw {
export using render::GLFWRenderPlugin;
}