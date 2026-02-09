export module epix.render:graph.context;

import webgpu;

import :graph.decl;
import :graph.slot;
import :graph.node;

using namespace core;

namespace render::graph {
struct RunSubGraph {
    GraphLabel id;
    std::vector<SlotValue> inputs;
    std::optional<Entity> view_entity;
};
/**
 * @brief GraphContext provides the context for a node to run in the render graph.
 * It is used to set outputs and get inputs, and to run sub-graphs.
 */
export struct GraphContext {
   private:
    const RenderGraph& m_graph;
    const NodeState& m_node_state;
    const std::vector<SlotValue>& m_inputs;
    std::vector<std::optional<SlotValue>>& m_outputs;
    std::vector<RunSubGraph> m_sub_graphs;
    std::optional<Entity> m_view_entity;

   public:
    GraphContext(const RenderGraph& graph,
                 const NodeState& node_state,
                 const std::vector<SlotValue>& inputs,
                 std::vector<std::optional<SlotValue>>& outputs)
        : m_graph(graph), m_node_state(node_state), m_inputs(inputs), m_outputs(outputs) {}
    const std::vector<SlotValue>& inputs() const { return m_inputs; }
    const SlotInfos& input_info() const { return m_node_state.inputs; }
    const SlotInfos& output_info() const { return m_node_state.outputs; }
    const SlotValue* get_input(const SlotLabel& label) const {
        return m_node_state.inputs.get_slot_index(label)
            .transform([&](uint32_t index) -> const SlotValue* { return &m_inputs[index]; })
            .value_or(nullptr);
    }
    std::optional<Entity> get_input_entity(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->entity() : std::nullopt;
    }
    std::optional<wgpu::Buffer> get_input_buffer(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->buffer() : std::nullopt;
    }
    std::optional<wgpu::TextureView> get_input_texture(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->texture() : std::nullopt;
    }
    std::optional<wgpu::Sampler> get_input_sampler(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->sampler() : std::nullopt;
    }

    bool set_output(const SlotLabel& label, SlotValue value) {
        auto index = m_node_state.outputs.get_slot_index(label);
        auto info  = m_node_state.outputs.get_slot(label);
        if (index && info) {
            if (info->get().type == value.type()) {
                m_outputs[*index].emplace(std::move(value));
                return true;
            }
        }
        return false;
    }

    Entity view_entity() const { return m_view_entity.value(); }
    std::optional<Entity> get_view_entity() const { return m_view_entity; }
    void set_view_entity(Entity entity) { m_view_entity = entity; }

    bool run_sub_graph(const GraphLabel& label,
                       std::span<const SlotValue> inputs,
                       std::optional<Entity> view_entity = std::nullopt);

    std::vector<RunSubGraph> finish() { return std::move(m_sub_graphs); }
};
/**
 * @brief RenderContext, stores the nvrhi device and command list.
 */
export struct RenderContext {
   private:
    wgpu::Device m_device;
    std::optional<wgpu::CommandEncoder> m_command_encoder;
    std::vector<wgpu::CommandBuffer> m_queued_commands;

   public:
    RenderContext(wgpu::Device device) : m_device(std::move(device)) {}

    const wgpu::Device& device() const { return m_device; }
    wgpu::CommandEncoder& command_encoder() {
        if (!m_command_encoder) {
            m_command_encoder = m_device.createCommandEncoder();
        }
        return *m_command_encoder;
    }
    wgpu::RenderPassEncoder begin_render_pass(const wgpu::RenderPassDescriptor& desc) {
        return command_encoder().beginRenderPass(desc);
    }
    void add_command_buffer(wgpu::CommandBuffer buffer) { m_queued_commands.emplace_back(std::move(buffer)); }
    void flush_encoder() {
        if (m_command_encoder) {
            m_queued_commands.emplace_back(m_command_encoder->finish({}));
            m_command_encoder.reset();
        }
    }
    std::vector<wgpu::CommandBuffer> finish() {
        flush_encoder();
        return std::move(m_queued_commands);
    }
};
}  // namespace render::graph