#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <functional>
#endif

#ifndef EPIX_CXX_MODULE
#include <epix/core.hpp>
#endif
#ifndef EPIX_CXX_MODULE
#include <webgpu/webgpu.hpp>
#endif
#ifndef EPIX_CXX_MODULE
#include <epix/shader.hpp>
#endif

#include <epix/render/assets.hpp>
#include <epix/render/extract.hpp>
#include <epix/render/graph.hpp>
#include <epix/render/image.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/schedule.hpp>
#include <epix/render/view.hpp>
#include <epix/render/window.hpp>

namespace epix::render {
/**
 * @brief Resource for anonymous surface that is used for requesting adapter/device.
 * Since webgpu requires a surface to request an adapter, we provide this resource to let window implementations to
 * give a functor that creates a surface from the instance. It is recommanded that the functor will destruct the
 * temporary window after we are done with requesting the adapter/device and releasing this resource.
 */
EPIX_EXPORT struct AnonymousSurface {
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
};
/** @brief Plugin that initializes the WebGPU rendering subsystem. */
EPIX_EXPORT struct RenderPlugin {
    /** @brief Validation level (0 = none, 1 = nvrhi, 2 = Vulkan validation
     * layers). */
    int validation = 0;
    /**
     * @brief Set the validation level for the render plugin.
     * 0 - No validation
     * 1 - Nvrhi validation
     * 2 - Vulkan validation layers
     * @param level the validation level to set
     */
    RenderPlugin& set_validation(int level = 0) noexcept;
    void attach(core::App&);
    void detach(core::App&) noexcept;
};
void render_system(core::World& world);
}  // namespace epix::render