#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <GLFW/glfw3.h>

#include <epix/core.hpp>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::glfw::render {
EPIX_EXPORT wgpu::Surface get_wgpu_surface(const wgpu::Instance& instance, GLFWwindow* window);

/** @brief Plugin that registers GLFW-specific render target (surface)
 * creation for the render pipeline. */
EPIX_EXPORT struct GLFWRenderPlugin {
    void attach(core::App& app);
};
}  // namespace epix::glfw::render

namespace epix::glfw {
EPIX_EXPORT using render::GLFWRenderPlugin;
}