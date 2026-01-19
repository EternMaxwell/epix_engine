export module epix.render:graph.decl;

import epix.core;

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
EPIX_MAKE_LABEL(NodeLabel)
EPIX_MAKE_LABEL(GraphLabel)
struct Node;
struct NodeState;
struct RenderGraph;
struct GraphContext;
struct RenderContext;
struct RunSubGraph;
}  // namespace render::graph