module;

#include <spdlog/spdlog.h>

module epix.core_graph;

namespace epix::core_graph {
void CoreGraphPlugin::attach(App& app) {
    spdlog::debug("[core_graph] Attaching CoreGraphPlugin.");
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
}
}  // namespace epix::core_graph
