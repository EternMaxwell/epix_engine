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
export EPIX_MAKE_LABEL(NodeLabel);
export EPIX_MAKE_LABEL(GraphLabel);
export struct Node;
struct NodeState;
export struct RenderGraph;
export struct GraphContext;
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