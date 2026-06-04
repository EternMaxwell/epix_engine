#pragma once

#include <GLFW/glfw3.h>

#include <epix/core.hpp>
#include <webgpu/webgpu.hpp>
namespace epix::glfw::render {
wgpu::Surface get_wgpu_surface(const wgpu::Instance& instance, GLFWwindow* window);

/** @brief Plugin that registers GLFW-specific render target (surface)
 * creation for the render pipeline. */
struct GLFWRenderPlugin {
    void attach(epix::core::App& app);
};
}  // namespace epix::glfw::render

namespace epix::glfw {
using render::GLFWRenderPlugin;
}  // namespace epix::glfw