#include "epix/render/graph.h"

using namespace epix::render::graph;
using namespace epix::render;

//==================== GraphContext ==================//

EPIX_API GraphContext::GraphContext(const RenderGraph& graph,
                                    const NodeState& node_state,
                                    const std::vector<SlotValue>& inputs,
                                    std::vector<std::optional<SlotValue>>& outputs)
    : m_graph(graph), m_node_state(node_state), m_inputs(inputs), m_outputs(outputs) {}
EPIX_API const std::vector<SlotValue>& GraphContext::inputs() const { return m_inputs; }
EPIX_API const SlotInfos& GraphContext::input_info() const { return m_node_state.inputs; }
EPIX_API const SlotInfos& GraphContext::output_info() const { return m_node_state.outputs; }
EPIX_API const SlotValue* GraphContext::get_input(const SlotLabel& label) const {
    auto index = m_node_state.inputs.get_slot_index(label);
    if (index) {
        return &m_inputs[*index];
    }
    return nullptr;
}
EPIX_API std::optional<epix::app::Entity> GraphContext::get_input_entity(const SlotLabel& label) const {
    auto value = get_input(label);
    if (value && value->is_entity()) {
        return value->entity();
    }
    return std::nullopt;
}
EPIX_API std::optional<nvrhi::BufferHandle> GraphContext::get_input_buffer(const SlotLabel& label) const {
    auto value = get_input(label);
    if (value && value->is_buffer()) {
        return value->buffer();
    }
    return std::nullopt;
}
EPIX_API std::optional<nvrhi::TextureHandle> GraphContext::get_input_texture(const SlotLabel& label) const {
    auto value = get_input(label);
    if (value && value->is_texture()) {
        return value->texture();
    }
    return std::nullopt;
}
EPIX_API std::optional<nvrhi::SamplerHandle> GraphContext::get_input_sampler(const SlotLabel& label) const {
    auto value = get_input(label);
    if (value && value->is_sampler()) {
        return value->sampler();
    }
    return std::nullopt;
}

EPIX_API bool GraphContext::set_output(const SlotLabel& label, const SlotValue& value) {
    auto index = m_node_state.outputs.get_slot_index(label);
    auto info  = m_node_state.outputs.get_slot(label);
    if (index && info) {
        if (info->type == value.type()) {
            m_outputs[*index].emplace(value);
            return true;
        } else {
            return false;
        }
    }
    return false;
}

EPIX_API epix::app::Entity GraphContext::view_entity() const { return m_view_entity.value(); }
EPIX_API std::optional<epix::app::Entity> GraphContext::get_view_entity() const { return m_view_entity; }
EPIX_API void GraphContext::set_view_entity(epix::app::Entity entity) { m_view_entity = entity; }

EPIX_API bool GraphContext::run_sub_graph(const GraphLabel& graph,
                                          epix::util::ArrayProxy<SlotValue> inputs,
                                          std::optional<epix::app::Entity> view_entity) {
    auto sub_graph = m_graph.get_sub_graph(graph);
    if (sub_graph) {
        // check the inputs matches the sub graph inputs
        if (auto input_node = sub_graph->get_input_node()) {
            size_t required_inputs_size = input_node->inputs.size();
            size_t inputs_size          = inputs.size();
            if (required_inputs_size != inputs_size) {
                spdlog::warn("[run_sub_graph] Sub graph {} has {} inputs, but {} inputs were provided.", graph.name(),
                             required_inputs_size, inputs_size);
                return false;
            }
            for (size_t i = 0; i < required_inputs_size; i++) {
                auto input = input_node->inputs.get_slot((uint32_t)i);
                if (input) {
                    auto value = *(inputs.begin() + i);
                    if (input->type != value.type()) {
                        spdlog::warn("[run_sub_graph] Sub graph {} input {} type mismatch. Expected {}, got {}.",
                                     graph.name(), input->name, type_name(input->type), type_name(value.type()));
                        return false;
                    }
                }
            }
        } else {
            if (!inputs.empty()) {
                spdlog::warn("[run_sub_graph] Sub graph {} has no input node, but {} inputs were provided.",
                             graph.name(), inputs.size());
                return false;
            }
        }
        m_sub_graphs.emplace_back(graph, std::vector<SlotValue>(inputs.begin(), inputs.end()), view_entity);
        return true;
    } else {
        spdlog::warn("[run_sub_graph] Sub graph {} not found.", graph.name());
    }
    return false;
}
EPIX_API std::vector<RunSubGraph> GraphContext::finish() { return std::move(m_sub_graphs); }
