export module epix.render:graph;

export import :graph.decl;
export import :graph.slot;
export import :graph.node;
export import :graph.context;
export import :graph.error;

import std;

namespace render::graph {
export struct RenderGraph {
    std::unordered_map<NodeLabel, NodeState> nodes;
    std::unordered_map<GraphLabel, RenderGraph> sub_graphs;

    RenderGraph()                              = default;
    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph(RenderGraph&&)                 = default;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph& operator=(RenderGraph&&)      = default;

    void update(World& world);
    bool set_input(std::span<const SlotInfo> inputs);
    std::optional<std::reference_wrapper<const NodeState>> get_input_node() const;
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

    std::optional<std::reference_wrapper<NodeState>> get_node_state(const NodeLabel& id);
    std::optional<std::reference_wrapper<const NodeState>> get_node_state(const NodeLabel& id) const;
    NodeState& node_state(const NodeLabel& id);
    const NodeState& node_state(const NodeLabel& id) const;

    std::expected<void, GraphError> add_sub_graph(const GraphLabel& id, RenderGraph&& graph);
    std::optional<std::reference_wrapper<RenderGraph>> get_sub_graph(const GraphLabel& id);
    std::optional<std::reference_wrapper<const RenderGraph>> get_sub_graph(const GraphLabel& id) const;
    RenderGraph& sub_graph(const GraphLabel& id);
    const RenderGraph& sub_graph(const GraphLabel& id) const;
    auto iter_nodes() const { return nodes | std::views::values; }
};

struct RenderGraphRunner {
    static bool run(const RenderGraph& graph,
                    const wgpu::Device& device,
                    const wgpu::Queue& queue,
                    World& world,
                    std::function<void(const wgpu::CommandEncoder&)> finalizer);

    static bool run_graph(const RenderGraph& graph,
                          std::optional<GraphLabel> sub_graph,
                          RenderContext& render_context,
                          World& world,
                          std::span<const SlotValue> inputs,
                          std::optional<Entity> view_entity);
};
}  // namespace render::graph

export namespace render {
using graph::GraphContext;
using graph::RenderContext;
using graph::RenderGraph;
}  // namespace render