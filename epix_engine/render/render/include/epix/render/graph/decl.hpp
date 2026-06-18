#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/core.hpp>
#include <functional>
#include <type_traits>
#endif
#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::epix::core::Label {                                        \
       public:                                                                        \
        type() noexcept = default;                                                    \
        template <typename T>                                                         \
        type(T t) noexcept                                                            \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

namespace epix::render::graph {
/** @brief Label type identifying a node within a render graph. */
EPIX_EXPORT EPIX_MAKE_LABEL(NodeLabel);
/** @brief Label type identifying a sub-graph within a render graph. */
EPIX_EXPORT EPIX_MAKE_LABEL(GraphLabel);
/** @brief Base class for render graph nodes. Override to implement custom
 * rendering logic. */
EPIX_EXPORT struct Node;
EPIX_EXPORT struct NodeState;
/** @brief Directed acyclic graph of render nodes that drives the rendering
 * pipeline. */
EPIX_EXPORT struct RenderGraph;
/** @brief Context passed to a Node during graph execution, providing input/output slot access and sub-graph
 * invocation. */
EPIX_EXPORT struct GraphContext;
/** @brief Context providing GPU device access, command encoding, and render
 * pass creation during graph execution. */
EPIX_EXPORT struct RenderContext;
struct RunSubGraph;
}  // namespace epix::render::graph

template <>
struct std::hash<epix::render::graph::NodeLabel> {
    std::size_t operator()(const epix::render::graph::NodeLabel& label) const noexcept {
        return std::hash<epix::core::Label>()(label);
    }
};
template <>
struct std::hash<epix::render::graph::GraphLabel> {
    std::size_t operator()(const epix::render::graph::GraphLabel& label) const noexcept {
        return std::hash<epix::core::Label>()(label);
    }
};