module;

#include <spdlog/spdlog.h>

module epix.core_graph;

namespace epix::core_graph {
void CoreGraphPlugin::build(App& app) {
    spdlog::debug("[core_graph] Building CoreGraphPlugin.");
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
}
}  // namespace epix::core_graph
