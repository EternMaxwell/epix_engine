export module epix.render:graph.node;

import :graph.decl;
import :graph.slot;

import std;
import epix.core;

using namespace epix::core;

namespace epix::render::graph {
/** @brief Base class for render graph nodes.
 *
 * Override `inputs()` / `outputs()` to declare slots, `update()` for
 * per-frame state updates, and `run()` to issue GPU commands.
 */
export struct Node {
    /** @brief Declare the input slots this node accepts. */
    virtual std::vector<SlotInfo> inputs() { return {}; }
    /** @brief Declare the output slots this node produces. */
    virtual std::vector<SlotInfo> outputs() { return {}; }
    /** @brief Called once per frame to update internal state before rendering. */
    virtual void update(const World&) {}
    /** @brief Execute this node's GPU commands during the render pass. */
    virtual void run(GraphContext&, RenderContext&, const World&) {}
};
/**
 * @brief An edge in the render graph.
 *
 * An edge can be either a node edge or a slot edge.
 *
 * The ordering is output_node before input_node.
 */
struct Edge {
    NodeLabel input_node;
    NodeLabel output_node;
    std::uint32_t input_index  = static_cast<std::uint32_t>(-1);  // set to -1 if not used
    std::uint32_t output_index = static_cast<std::uint32_t>(-1);  // set to -1 if not used

    static Edge node_edge(const NodeLabel& output_node, const NodeLabel& input_node) {
        return Edge{input_node, output_node};
    }
    static Edge slot_edge(const NodeLabel& output_node,
                          std::uint32_t output_index,
                          const NodeLabel& input_node,
                          std::uint32_t input_index) {
        return Edge{input_node, output_node, input_index, output_index};
    }

    bool operator==(const Edge& other) const = default;
    bool operator!=(const Edge& other) const = default;
    bool is_slot_edge() const { return input_index != -1 && output_index != -1; }
};
struct Edges {
   private:
    NodeLabel m_label;
    std::vector<Edge> m_input_edges;
    std::vector<Edge> m_output_edges;

   public:
    Edges(NodeLabel label) : m_label(label) {}
    Edges(const Edges&)            = default;
    Edges(Edges&&)                 = default;
    Edges& operator=(const Edges&) = default;
    Edges& operator=(Edges&&)      = default;

    NodeLabel label() const { return m_label; }
    const std::vector<Edge>& input_edges() const { return m_input_edges; }
    const std::vector<Edge>& output_edges() const { return m_output_edges; }
    bool has_input_edge(const Edge& edge) const;
    bool has_output_edge(const Edge& edge) const;
    void remove_input_edge(const Edge& edge);
    void remove_output_edge(const Edge& edge);
    void add_input_edge(const Edge& edge);
    void add_output_edge(const Edge& edge);
    const Edge* get_input_slot_edge(std::size_t index) const;
    const Edge* get_output_slot_edge(std::size_t index) const;
};
/** @brief Runtime state tracking a node in the graph, including its slot
 * metadata, edges, and the polymorphic Node instance. */
export struct NodeState {
    /** @brief Label identifying this node in the graph. */
    NodeLabel label;
    /** @brief Slot metadata for the node's inputs. */
    SlotInfos inputs;
    /** @brief Slot metadata for the node's outputs. */
    SlotInfos outputs;
    /** @brief Owning pointer to the polymorphic Node implementation. */
    std::unique_ptr<Node> pnode;
    /** @brief Edges connecting this node to other nodes. */
    Edges edges;

    NodeState(NodeLabel id, Node* node)
        : label(id), pnode(node), edges(id), inputs(node->inputs()), outputs(node->outputs()) {}
    template <typename T>
        requires std::derived_from<std::decay_t<T>, Node>
    NodeState(NodeLabel id, T&& node)
        : label(id),
          pnode(std::make_unique<std::decay_t<T>>(std::forward<T>(node))),
          edges(id),
          inputs(node.inputs()),
          outputs(node.outputs()) {}

    template <typename T>
    T* node() {
        return dynamic_cast<T*>(pnode.get());
    }
    template <typename T>
    const T* node() const {
        return dynamic_cast<const T*>(pnode.get());
    }

    bool validate_input_slots() {
        for (std::size_t i = 0; i < inputs.size(); i++) {
            auto edge = edges.get_input_slot_edge(i);
            if (!edge) {
                return false;
            }
        }
        return true;
    }
    bool validate_output_slots() {
        for (std::size_t i = 0; i < outputs.size(); i++) {
            auto edge = edges.get_output_slot_edge(i);
            if (!edge) {
                return false;
            }
        }
        return true;
    }
};

struct GraphInputT {};
/** @brief Singleton label used to identify the graph input node. */
export GraphInputT GraphInput;
/** @brief Special node that forwards graph-level inputs to its outputs,
 * acting as the entry point for slot data. */
export struct GraphInputNode : public Node {
    std::vector<SlotInfo> m_inputs;
    GraphInputNode() = default;
    GraphInputNode(std::vector<SlotInfo> inputs) : m_inputs(std::move(inputs)) {}
    std::vector<SlotInfo> inputs() override { return m_inputs; }
    std::vector<SlotInfo> outputs() override { return m_inputs; }
    void run(GraphContext& graph, RenderContext&, const World&) override;
};
/** @brief A no-op node that does nothing when run. */
export struct EmptyNode : public Node {
    void run(GraphContext&, RenderContext&, const World&) override {}
};
}  // namespace render::graph