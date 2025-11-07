#pragma once

#include "graph/context.hpp"
#include "graph/error.hpp"
#include "graph/fwd.hpp"
#include "graph/node.hpp"
#include "graph/slot.hpp"

namespace epix::render::graph {
struct RenderGraph {
    std::unordered_map<NodeLabel, NodeState> nodes;
    std::unordered_map<GraphLabel, RenderGraph> sub_graphs;

    RenderGraph()                              = default;
    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph(RenderGraph&&)                 = default;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph& operator=(RenderGraph&&)      = default;

    void update(World& world);
    bool set_input(std::span<const SlotInfo> inputs);
    const NodeState* get_input_node() const;
    const NodeState& input_node() const;

    template <std::derived_from<Node> T, typename... Args>
    void add_node(const NodeLabel& id, Args&&... args) {
        nodes.emplace(id, NodeState(id, new T(std::forward<Args>(args)...)));
    }
    template <typename T>
        requires std::derived_from<std::decay_t<T>, Node>
    void add_node(const NodeLabel& id, T&& node) {
        nodes.emplace(id, NodeState(id, node));
    }

    std::expected<void, GraphError> remove_node(const NodeLabel& id);

    template <typename... Args>
    void add_node_edges(Args&&... args) {
        std::array<NodeLabel, sizeof...(args)> nodes{args...};
        for (auto&& [node, next_node] : nodes | std::views::adjacent<2>) {
            auto res = try_add_node_edge(node, next_node);
        }
    }

    std::expected<void, GraphError> try_add_node_edge(const NodeLabel& output_node, const NodeLabel& input_node);

    void add_node_edge(const NodeLabel& output_node, const NodeLabel& input_node);
    std::expected<void, GraphError> try_add_slot_edge(const NodeLabel& output_node,
                                                      const SlotLabel& output_slot,
                                                      const NodeLabel& input_node,
                                                      const SlotLabel& input_slot);
    void add_slot_edge(const NodeLabel& output_node,
                       const SlotLabel& output_slot,
                       const NodeLabel& input_node,
                       const SlotLabel& input_slot);

    std::expected<void, GraphError> remove_slot_edge(const NodeLabel& output_node,
                                                     const SlotLabel& output_slot,
                                                     const NodeLabel& input_node,
                                                     const SlotLabel& input_slot);
    std::expected<void, GraphError> remove_node_edge(const NodeLabel& output_node, const NodeLabel& input_node);

    std::expected<void, EdgeError> validate_edge(const Edge& edge, bool should_exist);
    bool has_edge(const Edge& edge) const;

    NodeState* get_node_state(const NodeLabel& id);
    const NodeState* get_node_state(const NodeLabel& id) const;
    NodeState& node_state(const NodeLabel& id);
    const NodeState& node_state(const NodeLabel& id) const;

    std::expected<void, GraphError> add_sub_graph(const GraphLabel& id, RenderGraph&& graph);
    RenderGraph* get_sub_graph(const GraphLabel& id);
    const RenderGraph* get_sub_graph(const GraphLabel& id) const;
    RenderGraph& sub_graph(const GraphLabel& id);
    const RenderGraph& sub_graph(const GraphLabel& id) const;
    auto iter_nodes() const { return nodes | std::views::values; }
};

struct RenderGraphRunner {
    static bool run(const RenderGraph& graph,
                    nvrhi::DeviceHandle device,
                    World& world,
                    std::function<void(nvrhi::CommandListHandle)> finalizer);

    static bool run_graph(const RenderGraph& graph,
                          std::optional<GraphLabel> sub_graph,
                          RenderContext& render_context,
                          World& world,
                          std::span<const SlotValue> inputs,
                          std::optional<Entity> view_entity);
};
}  // namespace epix::render::graph