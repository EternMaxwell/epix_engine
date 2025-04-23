#pragma once

#include <epix/app.h>
#include <epix/wgpu.h>

#include <optional>
#include <string>
#include <type_traits>
#include <typeindex>
#include <variant>
#include <vector>

namespace epix::render::graph {
struct GraphId {
    std::type_index type;
    size_t id;
    template <typename T>
    GraphId(T t) : type(typeid(T)), id(0) {
        if constexpr (std::is_enum_v<T>) {
            id = static_cast<size_t>(t);
        }
    }
    GraphId() : type(typeid(void)), id(0) {}
    GraphId(const GraphId&)            = default;
    GraphId(GraphId&&)                 = default;
    GraphId& operator=(const GraphId&) = default;
    GraphId& operator=(GraphId&&)      = default;
    bool operator==(const GraphId& other) const {
        return type == other.type && id == other.id;
    }
    bool operator!=(const GraphId& other) const { return !(*this == other); }
};
struct GraphIdHash {
    size_t operator()(const GraphId& id) const {
        return std::hash<std::type_index>()(id.type) ^
               std::hash<size_t>()(id.id);
    }
};
struct NodeId {
    std::type_index type;
    size_t id;
    template <typename T>
    NodeId(T t) : type(typeid(T)), id(0) {
        if constexpr (std::is_enum_v<T>) {
            id = static_cast<size_t>(t);
        }
    }
    NodeId() : type(typeid(void)), id(0) {}
    NodeId(const NodeId&)            = default;
    NodeId(NodeId&&)                 = default;
    NodeId& operator=(const NodeId&) = default;
    NodeId& operator=(NodeId&&)      = default;
    bool operator==(const NodeId& other) const {
        return type == other.type && id == other.id;
    }
    bool operator!=(const NodeId& other) const { return !(*this == other); }
};
struct NodeIdHash {
    size_t operator()(const NodeId& id) const {
        return std::hash<std::type_index>()(id.type) ^
               std::hash<size_t>()(id.id);
    }
};
enum class SlotType {
    Buffer,       // A buffer
    TextureView,  // A texture view
    Sampler,      // A sampler
    Entity,       // An entity from ecs world
};
struct SlotInfo {
    std::string name;
    SlotType type;
};
struct SlotValue {
    std::variant<
        epix::app::Entity,
        wgpu::Buffer,
        wgpu::TextureView,
        wgpu::Sampler>
        value;
};
struct Node;
struct NodeState;
struct RenderGraph;
struct RunSubGraph {
    GraphId id;
    std::vector<SlotValue> inputs;
    std::optional<epix::app::Entity> view_entity;
};
struct RenderGraphContext {
    const RenderGraph& graph;
    const NodeState& node_state;
    const std::vector<SlotValue>& inputs;
    std::vector<std::optional<SlotValue>>& outputs;
    std::vector<RunSubGraph> sub_graphs;
    std::optional<epix::app::Entity> view_entity;
};
struct RenderContext {};
struct Node {
    virtual std::vector<SlotInfo> inputs() { return {}; }
    virtual std::vector<SlotInfo> outputs() { return {}; }
    virtual void update(epix::app::World&) {}
    virtual void run(RenderGraphContext&, RenderContext&, epix::app::World&) {}
};
struct Edge {
    NodeId input_node;
    NodeId output_node;
    size_t input_index  = -1;  // set to -1 if not used
    size_t output_index = -1;  // set to -1 if not used

    bool operator==(const Edge& other) const {
        return input_node == other.input_node &&
               output_node == other.output_node &&
               input_index == other.input_index &&
               output_index == other.output_index;
    }
    bool operator!=(const Edge& other) const { return !(*this == other); }
};
struct Edges {
   private:
    NodeId m_label;
    std::vector<Edge> m_input_edges;
    std::vector<Edge> m_output_edges;

   public:
    Edges(NodeId label) : m_label(label) {}
    Edges(const Edges&)            = default;
    Edges(Edges&&)                 = default;
    Edges& operator=(const Edges&) = default;
    Edges& operator=(Edges&&)      = default;

    NodeId label() const { return m_label; }
    const std::vector<Edge>& input_edges() const { return m_input_edges; }
    const std::vector<Edge>& output_edges() const { return m_output_edges; }
    bool has_input_edge(const Edge& edge) const {
        return std::find_if(
                   m_input_edges.begin(), m_input_edges.end(),
                   [&edge](const Edge& e) { return e == edge; }
               ) != m_input_edges.end();
    }
    bool has_output_edge(const Edge& edge) const {
        return std::find_if(
                   m_output_edges.begin(), m_output_edges.end(),
                   [&edge](const Edge& e) { return e == edge; }
               ) != m_output_edges.end();
    }
    void remove_input_edge(const Edge& edge) {
        auto index = std::find_if(
            m_input_edges.begin(), m_input_edges.end(),
            [&edge](const Edge& e) { return e == edge; }
        );
        if (index != m_input_edges.end()) {
            m_input_edges.erase(index);
        }
    }
    void remove_output_edge(const Edge& edge) {
        auto index = std::find_if(
            m_output_edges.begin(), m_output_edges.end(),
            [&edge](const Edge& e) { return e == edge; }
        );
        if (index != m_output_edges.end()) {
            m_output_edges.erase(index);
        }
    }
    void add_input_edge(const Edge& edge) {
        if (!has_input_edge(edge)) {
            m_input_edges.push_back(edge);
        }
    }
    void add_output_edge(const Edge& edge) {
        if (!has_output_edge(edge)) {
            m_output_edges.push_back(edge);
        }
    }
    Edge* get_input_slot_edge(size_t index) {
        auto iter = std::find_if(
            m_input_edges.begin(), m_input_edges.end(),
            [&index](const Edge& e) { return e.input_index == index; }
        );
        if (iter != m_input_edges.end()) {
            return &(*iter);
        }
        return nullptr;
    }
    Edge* get_output_slot_edge(size_t index) {
        auto iter = std::find_if(
            m_output_edges.begin(), m_output_edges.end(),
            [&index](const Edge& e) { return e.output_index == index; }
        );
        if (iter != m_output_edges.end()) {
            return &(*iter);
        }
        return nullptr;
    }
};
struct NodeState {
    NodeId label;
    std::vector<SlotInfo> inputs;
    std::vector<SlotInfo> outputs;
    std::unique_ptr<Node> node;
    Edges edges;
};
struct RenderGraph {
    entt::dense_map<NodeId, NodeState, NodeIdHash> nodes;
    entt::dense_map<GraphId, RenderGraph, GraphIdHash> sub_graphs;

    void update(epix::app::World& world) {
        for (auto&& [id, node] : nodes) {
            node.node->update(world);
        }
        for (auto&& [id, sub_graph] : sub_graphs) {
            sub_graph.update(world);
        }
    }
};
}  // namespace epix::render::graph