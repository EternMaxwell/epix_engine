#pragma once

#include "../vulkan.hpp"
#include "fwd.hpp"
#include "node.hpp"
#include "slot.hpp"

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
struct GraphContext {
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
    std::optional<nvrhi::BufferHandle> get_input_buffer(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->buffer() : std::nullopt;
    }
    std::optional<nvrhi::TextureHandle> get_input_texture(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->texture() : std::nullopt;
    }
    std::optional<nvrhi::SamplerHandle> get_input_sampler(const SlotLabel& label) const {
        auto value = get_input(label);
        return value ? value->sampler() : std::nullopt;
    }

    bool set_output(const SlotLabel& label, const SlotValue& value) {
        auto index = m_node_state.outputs.get_slot_index(label);
        auto info  = m_node_state.outputs.get_slot(label);
        if (index && info) {
            if (info->type == value.type()) {
                m_outputs[*index].emplace(value);
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
struct RenderContext {
   private:
    nvrhi::DeviceHandle m_device;
    std::optional<nvrhi::CommandListHandle> m_command_list;
    std::vector<nvrhi::CommandListHandle> m_closed_command_lists;

   public:
    RenderContext(nvrhi::DeviceHandle device) : m_device(device) {}

    nvrhi::DeviceHandle device() const { return m_device; }
    nvrhi::CommandListHandle commands() {
        if (!m_command_list) {
            m_command_list =
                m_device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
            m_command_list.value()->open();
        }
        return *m_command_list;
    }
    nvrhi::CommandListHandle begin_render_pass(const nvrhi::GraphicsState& state) {
        auto cmd_list = commands();
        cmd_list->setGraphicsState(state);
        return cmd_list;
    }
    void add_command_list(const nvrhi::CommandListHandle& command_list) {
        m_closed_command_lists.push_back(command_list);
    }
    void flush_encoder() {
        if (m_command_list) {
            m_command_list.value()->close();
            m_closed_command_lists.emplace_back(std::move(*m_command_list));
            m_command_list.reset();
        }
    }
    std::vector<nvrhi::CommandListHandle> finish() {
        flush_encoder();
        return std::move(m_closed_command_lists);
    }
};
}  // namespace epix::render::graph