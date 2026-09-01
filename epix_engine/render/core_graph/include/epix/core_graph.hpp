#pragma once

#include <epix/common.hpp>
#include <epix/core_graph/blit.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/core_graph/core2d.hpp>
#include <epix/core_graph/upscaling.hpp>

namespace epix::core_graph {
/** @brief Plugin that registers the core render graph and 2D rendering
 * pipeline. */
EPIX_EXPORT struct CoreGraphPlugin {
    void attach(app::App& app);
};
}  // namespace epix::core_graph
