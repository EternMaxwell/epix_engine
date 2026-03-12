export module epix.render:graph.decl;

import epix.core;
import std;

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::core::Label {                                              \
       public:                                                                        \
        type() = default;                                                             \
        template <typename T>                                                         \
        type(T t)                                                                     \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

using namespace core;

namespace render::graph {
/** @brief Label type identifying a node within a render graph. */
export EPIX_MAKE_LABEL(NodeLabel);
/** @brief Label type identifying a sub-graph within a render graph. */
export EPIX_MAKE_LABEL(GraphLabel);
/** @brief Base class for render graph nodes. Override to implement custom
 * rendering logic. */
export struct Node;
struct NodeState;
/** @brief Directed acyclic graph of render nodes that drives the rendering
 * pipeline. */
export struct RenderGraph;
/** @brief Context passed to a Node during graph execution, providing input/output slot access and sub-graph
 * invocation. */
export struct GraphContext;
/** @brief Context providing GPU device access, command encoding, and render
 * pass creation during graph execution. */
export struct RenderContext;
struct RunSubGraph;
}  // namespace render::graph

template <>
struct std::hash<render::graph::NodeLabel> {
    std::size_t operator()(const render::graph::NodeLabel& label) const noexcept {
        return std::hash<core::Label>()(label);
    }
};
template <>
struct std::hash<render::graph::GraphLabel> {
    std::size_t operator()(const render::graph::GraphLabel& label) const noexcept {
        return std::hash<core::Label>()(label);
    }
};