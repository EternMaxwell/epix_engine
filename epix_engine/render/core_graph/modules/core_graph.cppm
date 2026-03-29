module;

export module epix.core_graph;

export import :core2d;

namespace epix::core_graph {
/** @brief Plugin that registers the core render graph and 2D rendering
 * pipeline. */
export struct CoreGraphPlugin {
    void build(App& app);
};
}  // namespace core_graph