#include "epix/core_graph.hpp"

namespace epix::core_graph {
void CoreGraphPlugin::build(App& app) { app.add_plugins(render::core_2d::Core2dPlugin{}); }
}  // namespace epix::core_graph