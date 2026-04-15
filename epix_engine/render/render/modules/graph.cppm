export module epix.render:graph;

export import :graph.decl;
export import :graph.slot;
export import :graph.node;
export import :graph.context;
export import :graph.error;

import std;

namespace epix::render::graph {
/** @brief Directed acyclic graph of render nodes.
 *
 * Nodes are connected by node edges (execution order) and slot edges
 * (data flow). Sub-graphs can be added and invoked by nodes during
 * execution.
 */
export struct RenderGraph {
    std::unordered_map<NodeLabel, NodeState> nodes;
    std::unordered_map<GraphLabel, RenderGraph> sub_graphs;

    RenderGraph()                              = default;
    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph(RenderGraph&&)                 = default;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph& operator=(RenderGraph&&)      = default;

    /** @brief Update all nodes in the graph with the given world. */
    void update(World& world);
    /** @brief Set the graph's input slot layout. Returns true if the input node was created. */
    bool set_input(std::span<const SlotInfo> inputs);
    /** @brief Get the input node state, if any. */
    std::optional<std::reference_wrapper<const NodeState>> get_input_node() const;
    /** @brief Get the input node state. Throws if no input node exists. */
    const NodeState& input_node() const;

    /** @brief Add a render node constructed in-place.
     *  @tparam T Node type derived from Node. */
    template <std::derived_from<Node> T, typename... Args>
    void add_node(const NodeLabel& id, Args&&... args) {
        nodes.emplace(id, NodeState(id, new T(std::forward<Args>(args)...)));
    }
    /** @brief Add a render node by forwarding an existing node object. */
    template <typename T>
        requires std::derived_from<std::decay_t<T>, Node>
    void add_node(const NodeLabel& id, T&& node) {
        nodes.emplace(id, NodeState(id, node));
    }

    /** @brief Remove a node from the graph by label. */
    std::expected<void, GraphError> remove_node(const NodeLabel& id);

    /** @brief Add execution-order edges between a chain of node labels. */
    template <typename... Args>
    void add_node_edges(Args&&... args) {
        std::array<NodeLabel, sizeof...(args)> nodes{args...};
        for (auto&& [node, next_node] : std::views::adjacent<2>(nodes)) {
            auto res = try_add_node_edge(node, next_node);
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

export namespace epix::render {
using graph::GraphContext;
using graph::RenderContext;
using graph::RenderGraph;
}  // namespace render