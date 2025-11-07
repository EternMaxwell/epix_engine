#pragma once

#include <epix/core.hpp>

#include "epix/core/label.hpp"

namespace epix::render::graph {
EPIX_MAKE_LABEL(NodeLabel)
EPIX_MAKE_LABEL(GraphLabel)
struct Node;
struct NodeState;
struct RenderGraph;
struct GraphContext;
struct RenderContext;
struct RunSubGraph;
}  // namespace epix::render::graph