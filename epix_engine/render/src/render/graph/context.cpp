#include "epix/render/graph.hpp"
#include "epix/render/graph/context.hpp"

using namespace epix::render::graph;

bool GraphContext::run_sub_graph(const GraphLabel& graph,
                                 std::span<const SlotValue> inputs,
                                 std::optional<Entity> view_entity) {
    auto sub_graph = m_graph.get_sub_graph(graph);
    if (sub_graph) {
        // check the inputs matches the sub graph inputs
        if (auto input_node = sub_graph->get_input_node()) {
            size_t required_inputs_size = input_node->inputs.size();
            size_t inputs_size          = inputs.size();
            if (required_inputs_size != inputs_size) {
                spdlog::warn("[run_sub_graph] Sub graph {} has {} inputs, but {} inputs were provided.",
                             graph.type_index().short_name(), required_inputs_size, inputs_size);
                return false;
            }
            for (size_t i = 0; i < required_inputs_size; i++) {
                auto input = input_node->inputs.get_slot((uint32_t)i);
                if (input) {
                    auto value = *(inputs.begin() + i);
                    if (input->type != value.type()) {
                        spdlog::warn("[run_sub_graph] Sub graph {} input {} type mismatch. Expected {}, got {}.",
                                     graph.type_index().short_name(), input->name, type_name(input->type),
                                     type_name(value.type()));
                        return false;
                    }
                }
            }
        } else {
            if (!inputs.empty()) {
                spdlog::warn("[run_sub_graph] Sub graph {} has no input node, but {} inputs were provided.",
                             graph.type_index().short_name(), inputs.size());
                return false;
            }
        }
        m_sub_graphs.emplace_back(graph, std::vector<SlotValue>(inputs.begin(), inputs.end()), view_entity);
        return true;
    } else {
        spdlog::warn("[run_sub_graph] Sub graph {} not found.", graph.type_index().short_name());
    }
    return false;
}