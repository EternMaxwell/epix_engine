module epix.core_graph;

namespace core_graph {
void CoreGraphPlugin::build(App& app) { app.add_plugins(core_graph::core_2d::Core2dPlugin{}); }
}  // namespace core_graph