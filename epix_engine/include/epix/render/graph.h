#pragma once

#include <epix/app.h>
#include <epix/utils/variant.h>
#include <epix/vulkan.h>

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
    Buffer,   // A buffer
    Texture,  // A texture
    Sampler,  // A sampler
    Entity,   // An entity from ecs world
};
EPIX_API std::string_view type_name(SlotType type);
struct SlotInfo {
    std::string name;
    SlotType type;
};
struct SlotValue {
   private:
    std::variant<epix::app::Entity,
                 nvrhi::BufferHandle,
                 nvrhi::TextureHandle,
                 nvrhi::SamplerHandle>
        value;

   public:
    EPIX_API SlotValue(const epix::app::Entity& entity);
    EPIX_API SlotValue(const nvrhi::BufferHandle& buffer);
    EPIX_API SlotValue(const nvrhi::TextureHandle& texture);
    EPIX_API SlotValue(const nvrhi::SamplerHandle& sampler);
    EPIX_API SlotType type() const;
    EPIX_API bool is_entity() const;
    EPIX_API bool is_buffer() const;
    EPIX_API bool is_texture() const;
    EPIX_API bool is_sampler() const;
    EPIX_API std::optional<epix::app::Entity> entity() const;
    EPIX_API std::optional<nvrhi::BufferHandle> buffer() const;
    EPIX_API std::optional<nvrhi::TextureHandle> texture() const;
    EPIX_API std::optional<nvrhi::SamplerHandle> sampler() const;
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
    EPIX_API std::optional<uint32_t> get_slot_index(
        const SlotLabel& label) const;
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
struct GraphContext {
   private:
    const RenderGraph& m_graph;
    const NodeState& m_node_state;
    const std::vector<SlotValue>& m_inputs;
    std::vector<std::optional<SlotValue>>& m_outputs;
    std::vector<RunSubGraph> m_sub_graphs;
    std::optional<epix::app::Entity> m_view_entity;

   public:
    EPIX_API GraphContext(const RenderGraph& graph,
                          const NodeState& node_state,
                          const std::vector<SlotValue>& inputs,
                          std::vector<std::optional<SlotValue>>& outputs);
    EPIX_API const std::vector<SlotValue>& inputs() const;
    EPIX_API const SlotInfos& input_info() const;
    EPIX_API const SlotInfos& output_info() const;
    EPIX_API const SlotValue* get_input(const SlotLabel& label) const;
    EPIX_API std::optional<epix::app::Entity> get_input_entity(
        const SlotLabel& label) const;
    EPIX_API std::optional<nvrhi::BufferHandle> get_input_buffer(
        const SlotLabel& label) const;
    EPIX_API std::optional<nvrhi::TextureHandle> get_input_texture(
        const SlotLabel& label) const;
    EPIX_API std::optional<nvrhi::SamplerHandle> get_input_sampler(
        const SlotLabel& label) const;

    EPIX_API bool set_output(const SlotLabel& label, const SlotValue& value);

    EPIX_API epix::app::Entity view_entity() const;
    EPIX_API std::optional<epix::app::Entity> get_view_entity() const;
    EPIX_API void set_view_entity(epix::app::Entity entity);

    EPIX_API bool run_sub_graph(
        const GraphLabel& label,
        epix::util::ArrayProxy<SlotValue> inputs,
        std::optional<epix::app::Entity> view_entity = std::nullopt);

    EPIX_API std::vector<RunSubGraph> finish();
};
struct RenderContext {
   private:
    nvrhi::DeviceHandle m_device;
    std::optional<nvrhi::CommandListHandle> m_command_list;
    std::vector<nvrhi::CommandListHandle> m_closed_command_lists;

   public:
    EPIX_API RenderContext(nvrhi::DeviceHandle device);

    EPIX_API nvrhi::DeviceHandle device() const;
    EPIX_API nvrhi::CommandListHandle commands();
    EPIX_API nvrhi::CommandListHandle begin_render_pass(
        const nvrhi::GraphicsState& state);
    EPIX_API void add_command_list(
        const nvrhi::CommandListHandle& command_list);
    EPIX_API void flush_encoder();
    EPIX_API std::vector<nvrhi::CommandListHandle> finish();
};

struct Node {
    virtual std::vector<SlotInfo> inputs() { return {}; }
    virtual std::vector<SlotInfo> outputs() { return {}; }
    virtual void update(epix::app::World&) {}
    virtual void run(GraphContext&, RenderContext&, epix::app::World&) {}
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
    uint32_t input_index  = static_cast<uint32_t>(-1);  // set to -1 if not used
    uint32_t output_index = static_cast<uint32_t>(-1);  // set to -1 if not used

    EPIX_API static Edge node_edge(const NodeLabel& output_node,
                                   const NodeLabel& input_node);
    EPIX_API static Edge slot_edge(const NodeLabel& output_node,
                                   uint32_t output_index,
                                   const NodeLabel& input_node,
                                   uint32_t input_index);

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
    GraphInputNode() = default;
    EPIX_API GraphInputNode(epix::util::ArrayProxy<SlotInfo> inputs);
    EPIX_API std::vector<SlotInfo> inputs() override;
    EPIX_API std::vector<SlotInfo> outputs() override;
    EPIX_API void run(GraphContext& graph,
                      RenderContext& ctx,
                      epix::app::World& world) override;
};

struct NodeNotPresent {
    NodeLabel label;
};
struct EdgeNodesNotPresent {
    NodeLabel output_node;
    NodeLabel input_node;
    bool missing_output;
    bool missing_input;
};
struct SlotNotPresent {
    NodeLabel output_node;
    SlotLabel output_slot;
    NodeLabel input_node;
    SlotLabel input_slot;
    bool missing_in_output;
    bool missing_in_input;
};
struct InputSlotOccupied {
    NodeLabel input_node;
    uint32_t input_index;
    NodeLabel current_output_node;
    uint32_t current_output_index;
    NodeLabel required_output_node;
    uint32_t required_output_index;
};
struct SlotTypeMismatch {
    NodeLabel output_node;
    uint32_t output_index;
    NodeLabel input_node;
    uint32_t input_index;
    SlotType output_type;
    SlotType input_type;
};
struct EdgeError : std::variant<EdgeNodesNotPresent,
                                SlotNotPresent,
                                InputSlotOccupied,
                                SlotTypeMismatch> {
    using std::variant<EdgeNodesNotPresent,
                       SlotNotPresent,
                       InputSlotOccupied,
                       SlotTypeMismatch>::variant;
};
struct SubGraphExists {
    GraphLabel id;
};

struct GraphError : std::variant<NodeNotPresent, EdgeError, SubGraphExists> {
    using std::variant<NodeNotPresent, EdgeError, SubGraphExists>::variant;
    EPIX_API std::string to_string() const;
};
EPIX_API std::string error_to_string(const GraphError& error);

struct RenderGraph {
    entt::dense_map<NodeLabel, NodeState> nodes;
    entt::dense_map<GraphLabel, RenderGraph> sub_graphs;

    EPIX_API void update(epix::app::World& world);
    EPIX_API bool set_input(epix::util::ArrayProxy<SlotInfo> inputs);
    EPIX_API const NodeState* get_input_node() const;
    EPIX_API const NodeState& input_node() const;

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
            try_add_node_edge(node, next_node);
        }
    }

    EPIX_API std::expected<void, GraphError> try_add_node_edge(
        const NodeLabel& output_node, const NodeLabel& input_node);

    EPIX_API void add_node_edge(const NodeLabel& output_node,
                                const NodeLabel& input_node);
    EPIX_API std::expected<void, GraphError> try_add_slot_edge(
        const NodeLabel& output_node,
        const SlotLabel& output_slot,
        const NodeLabel& input_node,
        const SlotLabel& input_slot);
    EPIX_API void add_slot_edge(const NodeLabel& output_node,
                                const SlotLabel& output_slot,
                                const NodeLabel& input_node,
                                const SlotLabel& input_slot);

    EPIX_API std::expected<void, GraphError> remove_slot_edge(
        const NodeLabel& output_node,
        const SlotLabel& output_slot,
        const NodeLabel& input_node,
        const SlotLabel& input_slot);
    EPIX_API std::expected<void, GraphError> remove_node_edge(
        const NodeLabel& output_node, const NodeLabel& input_node);

    EPIX_API std::expected<void, EdgeError> validate_edge(const Edge& edge,
                                                          bool should_exist);
    EPIX_API bool has_edge(const Edge& edge) const;

    EPIX_API NodeState* get_node_state(const NodeLabel& id);
    EPIX_API const NodeState* get_node_state(const NodeLabel& id) const;
    EPIX_API NodeState& node_state(const NodeLabel& id);
    EPIX_API const NodeState& node_state(const NodeLabel& id) const;

    EPIX_API std::expected<void, GraphError> add_sub_graph(const GraphLabel& id,
                                                           RenderGraph&& graph);
    EPIX_API RenderGraph* get_sub_graph(const GraphLabel& id);
    EPIX_API const RenderGraph* get_sub_graph(const GraphLabel& id) const;
    EPIX_API RenderGraph& sub_graph(const GraphLabel& id);
    EPIX_API const RenderGraph& sub_graph(const GraphLabel& id) const;

    using nodes_iterable =
        decltype((std::as_const(nodes) | std::views::values));

    EPIX_API nodes_iterable iter_nodes() const;
};

struct RenderGraphRunner {
    EPIX_API static bool run(const RenderGraph& graph,
                      nvrhi::DeviceHandle device,
                      World& world,
                      std::function<void(nvrhi::CommandListHandle)> finalizer);

    EPIX_API static bool run_graph(const RenderGraph& graph,
                            std::optional<GraphLabel> sub_graph,
                            RenderContext& render_context,
                            epix::app::World& world,
                            epix::util::ArrayProxy<SlotValue> inputs,
                            std::optional<epix::app::Entity> view_entity);
};
}  // namespace epix::render::graph