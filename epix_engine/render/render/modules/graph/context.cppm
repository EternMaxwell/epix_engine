export module epix.render:graph.context;

import webgpu;

import :graph.decl;
import :graph.slot;
import :graph.node;

using namespace epix::core;

namespace epix::render::graph {
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
    /** @brief Construct the context for a node execution.
     * @param graph The parent render graph.
     * @param node_state The state of the node being run.
     * @param inputs Slot values provided as inputs.
     * @param outputs Mutable output slot storage. */
    GraphContext(const RenderGraph& graph,
                 const NodeState& node_state,
                 const std::vector<SlotValue>& inputs,
                 std::vector<std::optional<SlotValue>>& outputs)
        : m_graph(graph), m_node_state(node_state), m_inputs(inputs), m_outputs(outputs) {}
    /** @brief Get all input slot values. */
    const std::vector<SlotValue>& inputs() const { return m_inputs; }
    /** @brief Get the input slot declarations. */
    const SlotInfos& input_info() const { return m_node_state.inputs; }
    /** @brief Get the output slot declarations. */
    const SlotInfos& output_info() const { return m_node_state.outputs; }
    /** @brief Get a specific input slot value by label.
     * @return Pointer to the value, or nullptr if not found. */
    const SlotValue* get_input(const SlotLabel& label) const {
        return m_node_state.inputs.get_slot_index(label)
            .transform([&](std::uint32_t index) -> const SlotValue* { return &m_inputs[index]; })
            .value_or(nullptr);
    }
    /** @brief Get an Entity-typed input by label. */
    std::optional<Entity> get_input_entity(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->entity() : std::nullopt;
    }
    /** @brief Get a Buffer-typed input by label. */
    std::optional<wgpu::Buffer> get_input_buffer(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->buffer() : std::nullopt;
    }
    /** @brief Get a TextureView-typed input by label. */
    std::optional<wgpu::TextureView> get_input_texture(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->texture() : std::nullopt;
    }
    /** @brief Get a Sampler-typed input by label. */
    std::optional<wgpu::Sampler> get_input_sampler(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->sampler() : std::nullopt;
    }

    /** @brief Write a value to an output slot.
     * @return True if the slot was found and the type matched. */
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

    /** @brief Get the view entity assigned to this context.
     * @note Throws if no view entity is set. */
    Entity view_entity() const { return m_view_entity.value(); }
    /** @brief Get the view entity, or std::nullopt if none is set. */
    std::optional<Entity> get_view_entity() const { return m_view_entity; }
    /** @brief Assign a view entity to this context. */
    void set_view_entity(Entity entity) { m_view_entity = entity; }

    /** @brief Schedule a sub-graph to run after this node finishes.
     * @param label The sub-graph label.
     * @param inputs Input slot values for the sub-graph.
     * @param view_entity Optional view entity to pass to the sub-graph.
     * @return True if the sub-graph was found in the render graph. */
    bool run_sub_graph(const GraphLabel& label,
                       std::span<const SlotValue> inputs,
                       std::optional<Entity> view_entity = std::nullopt);

    /** @brief Consume and return all queued sub-graph runs. */
    std::vector<RunSubGraph> finish() { return std::move(m_sub_graphs); }
};
/**
 * @brief RenderContext, stores the wgpu device and command encoder.
 */
export struct RenderContext {
   private:
    wgpu::Device m_device;
    std::optional<wgpu::CommandEncoder> m_command_encoder;
    std::vector<wgpu::CommandBuffer> m_queued_commands;

   public:
    /** @brief Construct a render context with the given WebGPU device. */
    RenderContext(wgpu::Device device) : m_device(std::move(device)) {}

    /** @brief Get the WebGPU device. */
    const wgpu::Device& device() const { return m_device; }
    /** @brief Get or lazily create the command encoder. */
    wgpu::CommandEncoder& command_encoder() {
        if (!m_command_encoder) {
            m_command_encoder = m_device.createCommandEncoder();
        }
        return *m_command_encoder;
    }
    /** @brief Begin a new render pass on the current command encoder. */
    wgpu::RenderPassEncoder begin_render_pass(const wgpu::RenderPassDescriptor& desc) {
        return command_encoder().beginRenderPass(desc);
    }
    /** @brief Enqueue an externally-created command buffer. */
    void add_command_buffer(wgpu::CommandBuffer buffer) { m_queued_commands.emplace_back(std::move(buffer)); }
    /** @brief Finish the current command encoder (if any) and enqueue it. */
    void flush_encoder() {
        if (m_command_encoder) {
            m_queued_commands.emplace_back(m_command_encoder->finish({}));
            m_command_encoder.reset();
        }
    }
    /** @brief Flush and return all command buffers for submission. */
    std::vector<wgpu::CommandBuffer> finish() {
        flush_encoder();
        return std::move(m_queued_commands);
    }
};
}  // namespace render::graph