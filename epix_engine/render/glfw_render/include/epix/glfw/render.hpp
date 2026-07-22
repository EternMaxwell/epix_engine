#pragma once

#ifndef EPIX_CXX_MODULE
#include <GLFW/glfw3.h>

#include <epix/app.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::glfw::render {
EPIX_EXPORT wgpu::Surface get_wgpu_surface(const wgpu::Instance& instance, GLFWwindow* window);

/** @brief Plugin that registers GLFW-specific render target (surface)
 * creation for the render pipeline. */
EPIX_EXPORT struct GLFWRenderPlugin {
    void attach(app::App& app);
};
}  // namespace epix::glfw::render

namespace epix::glfw {
EPIX_EXPORT using render::GLFWRenderPlugin;
}