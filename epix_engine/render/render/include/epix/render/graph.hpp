#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <array>
#include <concepts>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#endif

#include <epix/render/graph/context.hpp>
#include <epix/render/graph/decl.hpp>
#include <epix/render/graph/error.hpp>
#include <epix/render/graph/node.hpp>
#include <epix/render/graph/slot.hpp>
namespace epix::render::graph {
/** @brief Directed acyclic graph of render nodes.
 *
 * Nodes are connected by node edges (execution order) and slot edges
 * (data flow). Sub-graphs can be added and invoked by nodes during
 * execution.
 */
EPIX_EXPORT struct RenderGraph {
    std::unordered_map<NodeLabel, NodeState> nodes;
    std::unordered_map<GraphLabel, RenderGraph> sub_graphs;

    RenderGraph()                              = default;
    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph(RenderGraph&&)                 = default;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph& operator=(RenderGraph&&)      = default;

    /** @brief Update all nodes in the graph with the given world. */
    void update(epix::ecs::World& world);
    /** @brief Set the graph's input slot layout.
     * @throws std::runtime_error if called twice (Bevy panics, graph.rs:98-104). */
    void set_input(std::span<const SlotInfo> inputs);
    /** @brief Get the input node state, if any. */
    std::optional<std::reference_wrapper<const NodeState>> get_input_node() const;
    /** @brief Get the input node state. Throws if no input node exists. */
    const NodeState& input_node() const;

    /** @brief Add a render node constructed in-place.
     *  @tparam T Node type derived from Node. */
    template <std::derived_from<Node> T, typename... Args>
    void add_node(const NodeLabel& id, Args&&... args) {
        // Bevy HashMap::insert: a duplicate label REPLACES the node (graph.rs:135-142).
        nodes.erase(id);
        nodes.emplace(id, NodeState(id, new T(std::forward<Args>(args)...)));
    }
    /** @brief Add a render node by forwarding an existing node object. */
    template <typename T>
        requires std::derived_from<std::decay_t<T>, Node>
    void add_node(const NodeLabel& id, T&& node) {
        nodes.erase(id);
        nodes.emplace(id, NodeState(id, node));
    }

    /** @brief Remove a node from the graph by label. */
    std::expected<void, GraphError> remove_node(const NodeLabel& id);

    /** @brief Add execution-order edges between a chain of node labels.
     *
     * Redefining an existing edge is tolerated (Bevy add_node_edges), every
     * other error — e.g. a missing node label — is reported as an error so a
     * broken chain is caught at setup time instead of silently at runtime. */
    template <typename... Args>
    void add_node_edges(Args&&... args) {
        std::array<NodeLabel, sizeof...(args)> nodes{args...};
        for (auto&& [node, next_node] : std::views::adjacent<2>(nodes)) {
            auto res = try_add_node_edge(node, next_node);
            if (res) continue;
            // EdgeAlreadyExists is wrapped as GraphError(EdgeError(EdgeAlreadyExists)).
            if (const auto* edge_error = std::get_if<EdgeError>(&res.error());
                edge_error && std::holds_alternative<EdgeAlreadyExists>(*edge_error)) {
                continue;
            }
            spdlog::error("[render.graph] Failed to add edge '{}' -> '{}': {}", node.type_index().short_name(),
                          next_node.type_index().short_name(), res.error().to_string());
        }
    }

    /** @brief Try to add an execution-order edge between two nodes. */
    std::expected<void, GraphError> try_add_node_edge(const NodeLabel& output_node, const NodeLabel& input_node);

    /** @brief Add an execution-order edge. Panics on failure. */
    void add_node_edge(const NodeLabel& output_node, const NodeLabel& input_node);
    /** @brief Try to add a data-flow edge between node slots. */
    std::expected<void, GraphError> try_add_slot_edge(const NodeLabel& output_node,
                                                      const SlotLabel& output_slot,
                                                      const NodeLabel& input_node,
                                                      const SlotLabel& input_slot);
    /** @brief Add a data-flow edge between node slots. Panics on failure. */
    void add_slot_edge(const NodeLabel& output_node,
                       const SlotLabel& output_slot,
                       const NodeLabel& input_node,
                       const SlotLabel& input_slot);

    /** @brief Remove a data-flow edge between node slots. */
    std::expected<void, GraphError> remove_slot_edge(const NodeLabel& output_node,
                                                     const SlotLabel& output_slot,
                                                     const NodeLabel& input_node,
                                                     const SlotLabel& input_slot);
    /** @brief Remove an execution-order edge. */
    std::expected<void, GraphError> remove_node_edge(const NodeLabel& output_node, const NodeLabel& input_node);

    /** @brief Validate that an edge exists or does not exist as expected. */
    std::expected<void, EdgeError> validate_edge(const Edge& edge, bool should_exist);
    /** @brief Check whether the graph contains the given edge. */
    bool has_edge(const Edge& edge) const;

    /** @brief Get a mutable reference to a node state by label. */
    std::optional<std::reference_wrapper<NodeState>> get_node_state(const NodeLabel& id);
    /** @brief Get a const reference to a node state by label. */
    std::optional<std::reference_wrapper<const NodeState>> get_node_state(const NodeLabel& id) const;
    /** @brief Get a mutable node state reference. Throws if not found. */
    NodeState& node_state(const NodeLabel& id);
    /** @brief Get a const node state reference. Throws if not found. */
    const NodeState& node_state(const NodeLabel& id) const;

    /** @brief Add a named sub-graph to this graph. */
    std::expected<void, GraphError> add_sub_graph(const GraphLabel& id, RenderGraph&& graph);
    /** @brief Get a mutable reference to a sub-graph by label. */
    std::optional<std::reference_wrapper<RenderGraph>> get_sub_graph(const GraphLabel& id);
    /** @brief Get a const reference to a sub-graph by label. */
    std::optional<std::reference_wrapper<const RenderGraph>> get_sub_graph(const GraphLabel& id) const;
    /** @brief Get a mutable sub-graph reference. Throws if not found. */
    RenderGraph& sub_graph(const GraphLabel& id);
    /** @brief Get a const sub-graph reference. Throws if not found. */
    const RenderGraph& sub_graph(const GraphLabel& id) const;
    /** @brief Iterate over all node states in this graph. */
    auto iter_nodes() const { return std::views::values(nodes); }
    /** @brief Iterate over all node states, allowing modification (Bevy
     * RenderGraph::iter_nodes_mut). */
    auto iter_nodes_mut() {
        return std::views::values(nodes) | std::views::transform([](NodeState& n) -> NodeState& { return n; });
    }
    /** @brief Iterate over (label, graph) pairs of the sub graphs (Bevy
     * RenderGraph::iter_sub_graphs). */
    auto iter_sub_graphs() const {
        return sub_graphs | std::views::transform([](const auto& kv) -> std::pair<GraphLabel, const RenderGraph&> {
                   return {kv.first, kv.second};
               });
    }
    /** @brief Iterate over (label, graph) pairs of the sub graphs, allowing
     * modification (Bevy RenderGraph::iter_sub_graphs_mut). */
    auto iter_sub_graphs_mut() {
        return sub_graphs | std::views::transform(
                                [](auto& kv) -> std::pair<GraphLabel, RenderGraph&> { return {kv.first, kv.second}; });
    }
    /** @brief Remove a sub graph by label; no-op when absent (Bevy
     * RenderGraph::remove_sub_graph). */
    void remove_sub_graph(const GraphLabel& id) { sub_graphs.erase(id); }
    /** @brief Get the concrete node of type T by label; nullptr when absent
     * or a different type (Bevy RenderGraph::get_node<T>). */
    template <typename T>
    T* get_node(const NodeLabel& id) {
        auto state = get_node_state(id);
        return state ? state->get().template node<T>() : nullptr;
    }
    template <typename T>
    const T* get_node(const NodeLabel& id) const {
        auto state = get_node_state(id);
        return state ? state->get().template node<T>() : nullptr;
    }
};

struct RenderGraphRunner {
    static bool run(const RenderGraph& graph,
                    const wgpu::Device& device,
                    const wgpu::Queue& queue,
                    epix::ecs::World& world,
                    std::function<void(wgpu::CommandEncoder&)> finalizer);

    static bool run_graph(const RenderGraph& graph,
                          std::optional<GraphLabel> sub_graph,
                          RenderContext& render_context,
                          epix::ecs::World& world,
                          std::span<const SlotValue> inputs,
                          std::optional<epix::ecs::Entity> view_entity);
};
/**
 * @brief Bevy `RenderGraphExt` helpers (render_graph/app.rs), operating on the
 * render world that holds the `RenderGraph` resource.
 */

/** @brief Add a sub graph to the render graph (Bevy add_render_sub_graph). */
EPIX_EXPORT inline void add_render_sub_graph(epix::ecs::World& world, const GraphLabel& id, RenderGraph&& graph) {
    auto render_graph = world.get_resource_mut<RenderGraph>();
    if (render_graph) {
        render_graph->get().add_sub_graph(id, std::move(graph));
    }
}

/** @brief Add a node to a sub graph, constructing it in place (Bevy
 * add_render_graph_node<T: Node + FromWorld>). Warns if the graph or sub
 * graph is missing. */
template <std::derived_from<Node> T, typename... Args>
void add_render_graph_node(epix::ecs::World& world,
                           const GraphLabel& sub_graph,
                           const NodeLabel& node_label,
                           Args&&... args) {
    auto render_graph = world.get_resource_mut<RenderGraph>();
    if (!render_graph) {
        spdlog::warn("RenderGraph not found. Make sure you are using add_render_graph_node on the RenderApp.");
        return;
    }
    if (auto graph = render_graph->get().get_sub_graph(sub_graph)) {
        graph->get().template add_node<T>(node_label, std::forward<Args>(args)...);
    } else {
        spdlog::warn("Tried adding a render graph node to sub graph {} but the sub graph doesn't exist.",
                     sub_graph.type_index().short_name());
    }
}

/** @brief Add execution-order edges between the given node labels in a sub
 * graph (Bevy add_render_graph_edges). */
template <typename... Args>
void add_render_graph_edges(epix::ecs::World& world, const GraphLabel& sub_graph, Args&&... labels) {
    auto render_graph = world.get_resource_mut<RenderGraph>();
    if (!render_graph) return;
    if (auto graph = render_graph->get().get_sub_graph(sub_graph)) {
        graph->get().add_node_edges(std::forward<Args>(labels)...);
    } else {
        spdlog::warn("Tried adding render graph edges to sub graph {} but the sub graph doesn't exist.",
                     sub_graph.type_index().short_name());
    }
}

/** @brief Add one execution-order edge in a sub graph (Bevy
 * add_render_graph_edge). */
inline void add_render_graph_edge(epix::ecs::World& world,
                                  const GraphLabel& sub_graph,
                                  const NodeLabel& output_node,
                                  const NodeLabel& input_node) {
    auto render_graph = world.get_resource_mut<RenderGraph>();
    if (!render_graph) return;
    if (auto graph = render_graph->get().get_sub_graph(sub_graph)) {
        graph->get().add_node_edge(output_node, input_node);
    } else {
        spdlog::warn("Tried adding a render graph edge to sub graph {} but the sub graph doesn't exist.",
                     sub_graph.type_index().short_name());
    }
}

}  // namespace epix::render::graph

EPIX_EXPORT namespace epix::render {
    using graph::GraphContext;
    using graph::RenderContext;
    using graph::RenderGraph;
}  // namespace epix::render