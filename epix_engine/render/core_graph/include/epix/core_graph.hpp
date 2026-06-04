#pragma once

#include <epix/core_graph/core2d.hpp>

namespace epix::core_graph {
/** @brief Plugin that registers the core render graph and 2D rendering
 * pipeline. */
struct CoreGraphPlugin {
    void attach(epix::core::App& app);
};
}  // namespace epix::core_graph