#pragma once

#include <epix/app.h>
#include <epix/utils/variant.h>
#include <epix/wgpu.h>

#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <typeindex>
#include <variant>
#include <vector>

namespace epix::render::graph {
struct GraphLabel : public epix::app::Label {
    template <typename T>
    GraphLabel(T t) : epix::app::Label(t) {}
    // using epix::app::Label::operator==;
    // using epix::app::Label::operator!=;
};
struct NodeLabel : public epix::app::Label {
    template <typename T>
    NodeLabel(T t) : epix::app::Label(t) {}
    // using epix::app::Label::operator==;
    // using epix::app::Label::operator!=;
};
enum class SlotType {
    Buffer,       // A buffer
    TextureView,  // A texture view
    Sampler,      // A sampler
    Entity,       // An entity from ecs world
};
inline std::string_view type_name(SlotType type) {
    switch (type) {
        case SlotType::Buffer:
            return "Buffer";
        case SlotType::TextureView:
            return "TextureView";
        case SlotType::Sampler:
            return "Sampler";
        case SlotType::Entity:
            return "Entity";
    }
    return "Unknown";
}
struct SlotInfo {
    std::string name;
    SlotType type;
};
struct SlotValue {
   private:
    std::variant<
        epix::app::Entity,
        wgpu::Buffer,
        wgpu::TextureView,
        wgpu::Sampler>
        value;

   public:
    EPIX_API SlotValue(const epix::app::Entity& entity);
    EPIX_API SlotValue(const wgpu::Buffer& buffer);
    EPIX_API SlotValue(const wgpu::TextureView& texture_view);
    EPIX_API SlotValue(const wgpu::Sampler& sampler);
    EPIX_API SlotType type() const;
    EPIX_API bool is_entity() const;
    EPIX_API bool is_buffer() const;
    EPIX_API bool is_texture() const;
    EPIX_API bool is_sampler() const;
    EPIX_API epix::app::Entity* entity();
    EPIX_API wgpu::Buffer* buffer();
    EPIX_API wgpu::TextureView* texture();
    EPIX_API wgpu::Sampler* sampler();
    EPIX_API const epix::app::Entity* entity() const;
    EPIX_API const wgpu::Buffer* buffer() const;
    EPIX_API const wgpu::TextureView* texture() const;
    EPIX_API const wgpu::Sampler* sampler() const;
};
struct SlotLabel {
    std::variant<uint32_t, std::string> label;
    EPIX_API SlotLabel(uint32_t l);
    EPIX_API SlotLabel(const std::string& l);
    EPIX_API SlotLabel(const char* l);
    SlotLabel(const SlotLabel&)            = default;
    SlotLabel(SlotLabel&&)                 = default;
    SlotLabel& operator=(const SlotLabel&) = default;
    SlotLabel& operator=(SlotLabel&&)      = default;
};
struct SlotInfos {
   private:
    std::vector<SlotInfo> slots;

   public:
    EPIX_API SlotInfos(epix::util::ArrayProxy<SlotInfo> slots = {});
    SlotInfos(const SlotInfos&)            = default;
    SlotInfos(SlotInfos&&)                 = default;
    SlotInfos& operator=(const SlotInfos&) = default;
    SlotInfos& operator=(SlotInfos&&)      = default;

    EPIX_API size_t size() const;
    EPIX_API bool empty() const;
    EPIX_API SlotInfo* get_slot(const SlotLabel& label);
    EPIX_API const SlotInfo* get_slot(const SlotLabel& label) const;
    EPIX_API std::optional<uint32_t> get_slot_index(const SlotLabel& label
    ) const;
    using iterable =
        std::ranges::ref_view<std::vector<epix::render::graph::SlotInfo>>;
    using const_iterable =
        std::ranges::ref_view<const std::vector<epix::render::graph::SlotInfo>>;
    EPIX_API iterable iter();
    EPIX_API const_iterable iter() const;
};
struct Node;
struct NodeState;
struct RenderGraph;
struct RunSubGraph {
    GraphLabel id;
    std::vector<SlotValue> inputs;
    std::optional<epix::app::Entity> view_entity;
};
struct RenderGraphContext {
   private:
    const RenderGraph& m_graph;
    const NodeState& m_node_state;
    const std::vector<SlotValue>& m_inputs;
    std::vector<std::optional<SlotValue>>& m_outputs;
    std::vector<RunSubGraph> m_sub_graphs;
    std::optional<epix::app::Entity> m_view_entity;

   public:
    EPIX_API RenderGraphContext(
        const RenderGraph& graph,
        const NodeState& node_state,
        const std::vector<SlotValue>& inputs,
        std::vector<std::optional<SlotValue>>& outputs
    );
    EPIX_API const std::vector<SlotValue>& inputs() const;
    EPIX_API const SlotInfos& input_info() const;
    EPIX_API const SlotInfos& output_info() const;
    EPIX_API const SlotValue* get_input(const SlotLabel& label) const;
    EPIX_API const epix::app::Entity* get_input_entity(const SlotLabel& label
    ) const;
    EPIX_API const wgpu::Buffer* get_input_buffer(const SlotLabel& label) const;
    EPIX_API const wgpu::TextureView* get_input_texture(const SlotLabel& label
    ) const;
    EPIX_API const wgpu::Sampler* get_input_sampler(const SlotLabel& label
    ) const;

    EPIX_API bool set_output(const SlotLabel& label, const SlotValue& value);

    EPIX_API epix::app::Entity view_entity() const;
    EPIX_API std::optional<epix::app::Entity> get_view_entity() const;
    EPIX_API void set_view_entity(epix::app::Entity entity);

    EPIX_API bool run_sub_graph(
        const GraphLabel& label,
        epix::util::ArrayProxy<SlotValue> inputs,
        std::optional<epix::app::Entity> view_entity = std::nullopt
    );

    EPIX_API std::vector<RunSubGraph> finish();
};
struct RenderContext {};
struct Node {
    virtual std::vector<SlotInfo> inputs() { return {}; }
    virtual std::vector<SlotInfo> outputs() { return {}; }
    virtual void update(epix::app::World&) {}
    virtual void run(RenderGraphContext&, RenderContext&, epix::app::World&) {}
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
    uint32_t input_index  = -1;  // set to -1 if not used
    uint32_t output_index = -1;  // set to -1 if not used

    EPIX_API static Edge node_edge(
        const NodeLabel& output_node, const NodeLabel& input_node
    ) {
        return Edge{input_node, output_node};
    }
    EPIX_API static Edge slot_edge(
        const NodeLabel& output_node,
        uint32_t output_index,
        const NodeLabel& input_node,
        uint32_t input_index
    ) {
        return Edge{input_node, output_node, input_index, output_index};
    }

    EPIX_API bool operator==(const Edge& other) const;
    EPIX_API bool operator!=(const Edge& other) const;
    EPIX_API bool is_slot_edge() const;
};
struct Edges {
   private:
    NodeLabel m_label;
    std::vector<Edge> m_input_edges;
    std::vector<Edge> m_output_edges;

   public:
    EPIX_API Edges(NodeLabel label);
    Edges(const Edges&)            = default;
    Edges(Edges&&)                 = default;
    Edges& operator=(const Edges&) = default;
    Edges& operator=(Edges&&)      = default;

    EPIX_API NodeLabel label() const;
    EPIX_API const std::vector<Edge>& input_edges() const;
    EPIX_API const std::vector<Edge>& output_edges() const;
    EPIX_API bool has_input_edge(const Edge& edge) const;
    EPIX_API bool has_output_edge(const Edge& edge) const;
    EPIX_API void remove_input_edge(const Edge& edge);
    EPIX_API void remove_output_edge(const Edge& edge);
    EPIX_API void add_input_edge(const Edge& edge);
    EPIX_API void add_output_edge(const Edge& edge);
    EPIX_API const Edge* get_input_slot_edge(size_t index) const;
    EPIX_API const Edge* get_output_slot_edge(size_t index) const;
};
struct NodeState {
    NodeLabel label;
    SlotInfos inputs;
    SlotInfos outputs;
    std::unique_ptr<Node> pnode;
    Edges edges;

    EPIX_API NodeState(NodeLabel id, Node* node);
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

    EPIX_API bool validate_input_slots();
    EPIX_API bool validate_output_slots();
};
inline struct GraphInputT {
} GraphInput;
struct GraphInputNode : public Node {
    std::vector<SlotInfo> m_inputs;
    GraphInputNode() {}
    GraphInputNode(epix::util::ArrayProxy<SlotInfo> inputs)
        : m_inputs(inputs.begin(), inputs.end()) {}
    std::vector<SlotInfo> inputs() override { return m_inputs; }
    std::vector<SlotInfo> outputs() override { return m_inputs; }
    void run(
        RenderGraphContext& graph, RenderContext& ctx, epix::app::World& world
    ) override {
        for (auto&& [index, value] : std::views::enumerate(graph.inputs())) {
            graph.set_output(index, value);
        }
    }
};
struct RenderGraph {
    entt::dense_map<NodeLabel, NodeState> nodes;
    entt::dense_map<GraphLabel, RenderGraph> sub_graphs;

    void update(epix::app::World& world) {
        for (auto&& [id, node] : nodes) {
            node.pnode->update(world);
        }
        for (auto&& [id, sub_graph] : sub_graphs) {
            sub_graph.update(world);
        }
    }
    bool set_input(epix::util::ArrayProxy<SlotInfo> inputs) {
        if (nodes.contains(GraphInput)) {
            spdlog::warn("Graph input node already exists. Ignoring set_input."
            );
            return false;
        }
        nodes.emplace(
            GraphInput, NodeState(GraphInput, new GraphInputNode(inputs))
        );
        return true;
    }
    const NodeState* get_input_node() const {
        auto iter = nodes.find(GraphInput);
        if (iter != nodes.end()) {
            return &iter->second;
        }
        return nullptr;
    }
    const NodeState& input_node() const {
        auto iter = nodes.find(GraphInput);
        if (iter != nodes.end()) {
            return iter->second;
        }
        throw std::runtime_error("Input node not found.");
    }

    /**
     * @brief Add a node to the graph.
     *
     * If the node already exists, it will be replaced.
     *
     * @tparam T The type of the node. Must be derived from Node.
     * @param id The id of the node.
     * @param args The arguments used to construct the node.
     */
    template <std::derived_from<Node> T, typename... Args>
    void add_node(const NodeLabel& id, Args&&... args) {
        nodes.emplace(id, NodeState(id, new T(std::forward<Args>(args)...)));
    }
    /**
     * @brief Add a node to the graph using copy or move semantics.
     *
     * If the node already exists, it will be replaced.
     *
     * @tparam T The type of the node. Must be derived from Node.
     * @param id The id of the node.
     * @param node The node to add. This can be either lvalue or rvalue.
     */
    template <typename T>
        requires std::derived_from<std::decay_t<T>, Node>
    void add_node(const NodeLabel& id, T&& node) {
        nodes.emplace(id, NodeState(id, node));
    }

    bool remove_node(const NodeLabel& id) {
        if (auto state = get_node_state(id)) {
            for (auto&& edge : state->edges.input_edges()) {
                auto output_node = get_node_state(edge.output_node);
                if (output_node) {
                    output_node->edges.remove_output_edge(edge);
                }
            }
            for (auto&& edge : state->edges.output_edges()) {
                auto input_node = get_node_state(edge.input_node);
                if (input_node) {
                    input_node->edges.remove_input_edge(edge);
                }
            }
            nodes.erase(id);
            return true;
        }
        return false;
    }

    bool try_add_node_edge(
        const NodeLabel& output_node, const NodeLabel& input_node
    ) {
        auto edge  = Edge::node_edge(output_node, input_node);
        auto valid = validate_edge(edge, false);
        if (!valid) {
            spdlog::warn(
                "Node edge {} -> {} already exists or conflicting with "
                "existing edges. Ignoring add_node_edge.",
                output_node.name(), input_node.name()
            );
            return false;
        }

        auto input_node_state  = get_node_state(input_node);
        auto output_node_state = get_node_state(output_node);
        if (!input_node_state || !output_node_state) {
            spdlog::warn(
                "One of node {} or {} does not exist. Ignoring add_node_edge.",
                output_node.name(), input_node.name()
            );
            return false;
        }
        output_node_state->edges.add_output_edge(edge);
        input_node_state->edges.add_input_edge(edge);

        return true;
    }
    void add_node_edge(
        const NodeLabel& output_node, const NodeLabel& input_node
    ) {
        if (!try_add_node_edge(output_node, input_node)) {
            throw std::runtime_error(std::format(
                "Failed to add node edge {} -> {}.", output_node.name(),
                input_node.name()
            ));
        }
    }
    bool try_add_slot_edge(
        const NodeLabel& output_node,
        const SlotLabel& output_slot,
        const NodeLabel& input_node,
        const SlotLabel& input_slot
    ) {
        auto output_node_state = get_node_state(output_node);
        auto input_node_state  = get_node_state(input_node);
        if (!output_node_state || !input_node_state) {
            spdlog::warn(
                "One of node {} or {} does not exist. Ignoring add_slot_edge.",
                output_node.name(), input_node.name()
            );
            return false;
        }
        auto output_index =
            output_node_state->outputs.get_slot_index(output_slot);
        auto input_index = input_node_state->inputs.get_slot_index(input_slot);
        if (!output_index || !input_index) {
            spdlog::warn(
                "One of slot {} or {} does not exist. Ignoring add_slot_edge.",
                std::visit(
                    epix::util::visitor{
                        [](uint32_t l) -> std::string {
                            return std::to_string(l);
                        },
                        [](const std::string& l) -> std::string { return l; },
                    },
                    output_slot.label
                ),
                std::visit(
                    epix::util::visitor{
                        [](uint32_t l) -> std::string {
                            return std::to_string(l);
                        },
                        [](const std::string& l) -> std::string { return l; },
                    },
                    input_slot.label
                )
            );
            return false;
        }
        auto edge = Edge::slot_edge(
            output_node, *output_index, input_node, *input_index
        );
        auto valid = validate_edge(edge, false);
        if (!valid) {
            spdlog::warn(
                "Slot edge {} -> {} already exists or conflicting with "
                "existing edges. Ignoring add_slot_edge.",
                output_node.name(), input_node.name()
            );
            return false;
        }

        output_node_state->edges.add_output_edge(edge);
        input_node_state->edges.add_input_edge(edge);

        return true;
    }
    void add_slot_edge(
        const NodeLabel& output_node,
        const SlotLabel& output_slot,
        const NodeLabel& input_node,
        const SlotLabel& input_slot
    ) {
        if (!try_add_slot_edge(
                output_node, output_slot, input_node, input_slot
            )) {
            throw std::runtime_error(std::format(
                "Failed to add slot edge {}:{} -> {}:{}.", output_node.name(),
                std::visit(
                    epix::util::visitor{
                        [](uint32_t l) -> std::string {
                            return std::to_string(l);
                        },
                        [](const std::string& l) -> std::string { return l; },
                    },
                    output_slot.label
                ),
                input_node.name(),
                std::visit(
                    epix::util::visitor{
                        [](uint32_t l) -> std::string {
                            return std::to_string(l);
                        },
                        [](const std::string& l) -> std::string { return l; },
                    },
                    input_slot.label
                )
            ));
        }
    }

    bool remove_slot_edge(
        const NodeLabel& output_node,
        const SlotLabel& output_slot,
        const NodeLabel& input_node,
        const SlotLabel& input_slot
    ) {
        auto output_node_state = get_node_state(output_node);
        auto input_node_state  = get_node_state(input_node);
        if (!output_node_state || !input_node_state) {
            spdlog::warn(
                "One of node {} or {} does not exist. Ignoring "
                "remove_slot_edge.",
                output_node.name(), input_node.name()
            );
            return false;
        }
        auto output_index =
            output_node_state->outputs.get_slot_index(output_slot);
        auto input_index = input_node_state->inputs.get_slot_index(input_slot);
        if (!output_index || !input_index) {
            spdlog::warn(
                "One of slot {} or {} does not exist. Ignoring "
                "remove_slot_edge.",
                std::visit(
                    epix::util::visitor{
                        [](uint32_t l) -> std::string {
                            return std::to_string(l);
                        },
                        [](const std::string& l) -> std::string { return l; },
                    },
                    output_slot.label
                ),
                std::visit(
                    epix::util::visitor{
                        [](uint32_t l) -> std::string {
                            return std::to_string(l);
                        },
                        [](const std::string& l) -> std::string { return l; },
                    },
                    input_slot.label
                )
            );
            return false;
        }
        auto edge = Edge::slot_edge(
            output_node, *output_index, input_node, *input_index
        );
        if (validate_edge(edge, true)) {
            output_node_state->edges.remove_output_edge(edge);
            input_node_state->edges.remove_input_edge(edge);
            return true;
        }
        return false;
    }
    bool remove_node_edge(
        const NodeLabel& output_node, const NodeLabel& input_node
    ) {
        auto edge = Edge::node_edge(output_node, input_node);
        if (validate_edge(edge, true)) {
            auto output_node_state = get_node_state(output_node);
            auto input_node_state  = get_node_state(input_node);
            if (output_node_state && input_node_state) {
                output_node_state->edges.remove_output_edge(edge);
                input_node_state->edges.remove_input_edge(edge);
                return true;
            }
        }
        return false;
    }

    bool validate_edge(const Edge& edge, bool should_exist) {
        if (should_exist && !has_edge(edge)) {
            return false;
        } else if (!should_exist && has_edge(edge)) {
            return false;
        }
        if (!edge.is_slot_edge()) return true;
        // this is a slot edge, check if the slot matches.
        auto output_node = get_node_state(edge.output_node);
        auto input_node  = get_node_state(edge.input_node);
        if (!output_node || !input_node) {
            return false;
        }
        auto from_slot = output_node->outputs.get_slot(edge.output_index);
        auto to_slot   = input_node->inputs.get_slot(edge.input_index);
        if (!from_slot || !to_slot) {
            return false;
        }

        // check if the input's input slot has not been connected to any other
        // node if should_exist is false
        if (auto to_input_edge_it = std::find_if(
                input_node->edges.input_edges().begin(),
                input_node->edges.input_edges().end(),
                [&edge](const Edge& e) -> bool {
                    if (!e.is_slot_edge()) return false;
                    return e.input_index == edge.input_index;
                }
            );
            to_input_edge_it != input_node->edges.input_edges().end()) {
            if (should_exist) {
                return false;
            }
        }

        // check if the slot types match
        if (from_slot->type != to_slot->type) {
            return false;
        }

        return true;
    }
    bool has_edge(const Edge& edge) const {
        auto iter_from = nodes.find(edge.input_node);
        auto iter_to   = nodes.find(edge.output_node);
        if (iter_from == nodes.end() || iter_to == nodes.end()) {
            return false;
        }
        auto& from_node = iter_from->second;
        auto& to_node   = iter_to->second;
        if (from_node.edges.has_output_edge(edge) &&
            to_node.edges.has_input_edge(edge)) {
            return true;
        }
        return false;
    }

    NodeState* get_node_state(const NodeLabel& id) {
        auto iter = nodes.find(id);
        if (iter != nodes.end()) {
            return &iter->second;
        }
        return nullptr;
    }
    const NodeState* get_node_state(const NodeLabel& id) const {
        auto iter = nodes.find(id);
        if (iter != nodes.end()) {
            return &iter->second;
        }
        return nullptr;
    }
    NodeState& node_state(const NodeLabel& id) {
        auto iter = nodes.find(id);
        if (iter != nodes.end()) {
            return iter->second;
        }
        throw std::runtime_error(std::format("Node {} not found.", id.name()));
    }
    const NodeState& node_state(const NodeLabel& id) const {
        auto iter = nodes.find(id);
        if (iter != nodes.end()) {
            return iter->second;
        }
        throw std::runtime_error(std::format("Node {} not found.", id.name()));
    }

    bool add_sub_graph(const GraphLabel& id, RenderGraph&& graph) {
        if (sub_graphs.contains(id)) {
            spdlog::warn(
                "Sub graph {} already exists. Ignoring add_sub_graph.",
                id.name()
            );
            return false;
        }
        sub_graphs.emplace(id, std::move(graph));
        return true;
    }
    RenderGraph* get_sub_graph(const GraphLabel& id) {
        auto iter = sub_graphs.find(id);
        if (iter != sub_graphs.end()) {
            return &iter->second;
        }
        return nullptr;
    }
    const RenderGraph* get_sub_graph(const GraphLabel& id) const {
        auto iter = sub_graphs.find(id);
        if (iter != sub_graphs.end()) {
            return &iter->second;
        }
        return nullptr;
    }
    RenderGraph& sub_graph(const GraphLabel& id) {
        auto iter = sub_graphs.find(id);
        if (iter != sub_graphs.end()) {
            return iter->second;
        }
        throw std::runtime_error(
            std::format("Sub graph {} not found.", id.name())
        );
    }
    const RenderGraph& sub_graph(const GraphLabel& id) const {
        auto iter = sub_graphs.find(id);
        if (iter != sub_graphs.end()) {
            return iter->second;
        }
        throw std::runtime_error(
            std::format("Sub graph {} not found.", id.name())
        );
    }
};

struct RenderGraphRunner {};
}  // namespace epix::render::graph