#pragma once

#include <epix/common.hpp>
#include <epix/core_graph/blit.hpp>
#include <epix/core_graph/core2d.hpp>
#include <epix/core_graph/core3d.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/core_graph/fullscreen_material.hpp>
#include <epix/core_graph/mip_generation.hpp>
#include <epix/core_graph/oit.hpp>
#include <epix/core_graph/prepass.hpp>
#include <epix/core_graph/skybox.hpp>
#include <epix/core_graph/tonemapping.hpp>
#include <epix/core_graph/upscaling.hpp>

namespace epix::core_graph {
/** @brief Plugin that registers the core render graph and 2D rendering
 * pipeline. */
EPIX_EXPORT struct EPIX_API CoreGraphPlugin {
    void attach(app::App& app);
};
}  // namespace epix::core_graph
