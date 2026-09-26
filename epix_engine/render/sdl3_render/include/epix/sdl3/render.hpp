#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <webgpu/webgpu.hpp>
#endif

/** @brief Native SDL window handle; SDL stays private to the engine. */
struct SDL_Window;

namespace epix::sdl3::render {
EPIX_EXPORT wgpu::Surface get_wgpu_surface(const wgpu::Instance& instance, SDL_Window* window);

/** @brief Plugin that registers SDL3-specific render target (surface)
 * creation for the render pipeline. */
EPIX_EXPORT struct EPIX_API SDL3RenderPlugin {
    void attach(app::App& app);
};
}  // namespace epix::sdl3::render

namespace epix::sdl3 {
EPIX_EXPORT using render::SDL3RenderPlugin;
}
