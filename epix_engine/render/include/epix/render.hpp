#pragma once

#include <epix/api/macros.hpp>

#include "render/assets.hpp"
#include "render/camera.hpp"
#include "render/extract.hpp"
#include "render/graph.hpp"
#include "render/image.hpp"
#include "render/pipeline.hpp"
#include "render/render_phase.hpp"
#include "render/schedule.hpp"
#include "render/shader.hpp"
#include "render/view.hpp"
#include "render/vulkan.hpp"
#include "render/window.hpp"

EPIX_MODULE_EXPORT namespace epix::render {
struct RenderPlugin {
    int validation = 0;
    /**
     * @brief Set the validation level for the render plugin.
     * 0 - No validation
     * 1 - Nvrhi validation
     * 2 - Vulkan validation layers
     * @param level the validation level to set
     */
    RenderPlugin& set_validation(int level = 0);
    void build(epix::App&);
    void finish(epix::App&);
    void finalize(epix::App&);
};
void render_system(World& world);
}  // namespace epix::render