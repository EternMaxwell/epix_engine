#pragma once

#include <epix/app.h>
#include <epix/render/assets.h>
#include <epix/render/camera.h>
#include <epix/render/common.h>
#include <epix/render/core_graph/core_2d.h>
#include <epix/render/graph.h>
#include <epix/render/pipeline.h>
#include <epix/render/render_phase.h>
#include <epix/render/shader.h>
#include <epix/render/view.h>
#include <epix/render/window.h>
#include <epix/vulkan.h>

namespace epix::render {
struct RenderPlugin {
    int validation = 0;
    /**
     * @brief Set the validation level for the render plugin.
     * 0 - No validation
     * 1 - Nvrhi validation
     * 2 - Vulkan validation layers
     * @param level the validation level to set
     */
    EPIX_API RenderPlugin& set_validation(int level = 0);
    EPIX_API void build(epix::App&);
    EPIX_API void finalize(epix::App&);
};
EPIX_API void render_system(World& world);
}  // namespace epix::render