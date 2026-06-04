module;
#include <epix/core_graph.hpp>

export module epix.core_graph;

export namespace epix::core_graph {
using epix::core_graph::CoreGraphPlugin;
using epix::core_graph::core_2d::Core2d;
}  // namespace epix::core_graph

export namespace epix::core_graph::core_2d {
using epix::core_graph::core_2d::Camera2D;
using epix::core_graph::core_2d::Camera2DBundle;
using epix::core_graph::core_2d::Core2dGraph;
using epix::core_graph::core_2d::Core2dNodes;
using epix::core_graph::core_2d::Core2dPlugin;
using epix::core_graph::core_2d::Opaque2D;
using epix::core_graph::core_2d::Transparent2D;
using epix::core_graph::core_2d::UI2DItem;
}  // namespace epix::core_graph::core_2d
