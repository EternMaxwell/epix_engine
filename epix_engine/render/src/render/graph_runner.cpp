#include "epix/render/graph.hpp"

using namespace epix::render::graph;

bool RenderGraphRunner::run(const RenderGraph& graph,
                            nvrhi::DeviceHandle device,
                            World& world,
                            std::function<void(nvrhi::CommandListHandle)> finalizer) {
    RenderContext render_context(device);
    auto res = run_graph(graph, std::nullopt, render_context, world, {}, std::nullopt);
    if (!res) {
        spdlog::warn("Failed to run graph {}.", graph.get_input_node()->label.type_index().short_name());
        return false;
    }
    // finalize the command encoder
    if (finalizer) finalizer(render_context.commands());
    // submit generated cmd buffers
    auto command_buffers = render_context.finish();
    auto commands =
        command_buffers |
        std::views::transform([](const nvrhi::CommandListHandle& cmd) -> nvrhi::ICommandList* { return cmd; }) |
        std::ranges::to<std::vector>();
    if (commands.size()) device->executeCommandLists(commands.data(), commands.size());
    return true;
}

bool RenderGraphRunner::run_graph(const RenderGraph& graph,
                                  std::optional<GraphLabel> sub_graph,
                                  RenderContext& render_context,
                                  World& world,
                                  std::span<const SlotValue> inputs,
                                  std::optional<Entity> view_entity) {
    // store all outputs of nodes in a map
    std::unordered_map<NodeLabel, std::vector<SlotValue>> node_outputs;

    spdlog::debug("Running graph {}.", sub_graph ? sub_graph->type_index().short_name() : "main");

    auto node_queue = graph.iter_nodes() |
                      std::views::filter([](const NodeState& node) { return node.inputs.empty(); }) |
                      std::views::transform([](const NodeState& node) { return &node; }) |
                      std::ranges::to<std::deque<const NodeState*>>();

    if (auto input_node = graph.get_input_node()) {
        std::vector<SlotValue> input_values;
        for (auto&& [i, input_slot] : std::views::enumerate(input_node->inputs.iter())) {
            if (i < inputs.size()) {
                if (input_slot.type != inputs[i].type()) {
                    spdlog::warn("Input slot {} type mismatch. Expected {}, got {}.", input_slot.name,
                                 type_name(input_slot.type), type_name(inputs[i].type()));
                    return false;
                }
                input_values.push_back(inputs[i]);
            } else {
                return false;
            }
        }

        node_outputs.emplace(input_node->label, std::move(input_values));

        for (auto&& next_node :
             input_node->edges.output_edges() | std::views::transform([](const Edge& e) { return e.input_node; })) {
            if (auto state = graph.get_node_state(next_node)) {
                node_queue.push_back(state);
            }
        }
    }

    while (!node_queue.empty()) {
        auto node_state = node_queue.back();
        node_queue.pop_back();

        if (node_outputs.contains(node_state->label)) continue;

        std::vector<std::pair<uint32_t, SlotValue>> slot_indices_and_inputs;
        // check if all dependencies have finished running
        {
            bool break_loop = false;
            for (auto&& [edge, input_node] : node_state->edges.input_edges() | std::views::transform([](const Edge& e) {
                                                 return std::pair{e, e.output_node};
                                             })) {
                if (edge.is_slot_edge()) {
                    if (auto outputs_it = node_outputs.find(input_node); outputs_it != node_outputs.end()) {
                        auto&& outputs = outputs_it->second;
                        slot_indices_and_inputs.emplace_back(edge.input_index, outputs[edge.output_index]);
                    } else {
                        node_queue.push_front(node_state);
                        break_loop = true;
                        break;
                    }
                } else {
                    if (!node_outputs.contains(input_node)) {
                        node_queue.push_front(node_state);
                        break_loop = true;
                        break;
                    }
                }
            }
            if (break_loop) continue;
        }

        // construct the inputs for the node
        std::sort(slot_indices_and_inputs.begin(), slot_indices_and_inputs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        auto inputs = slot_indices_and_inputs | std::views::transform([](const auto& pair) { return pair.second; }) |
                      std::ranges::to<std::vector>();
        if (inputs.size() != node_state->inputs.size()) {
            spdlog::warn("Node {} input size mismatch. Expected {}, got {}.",
                         node_state->label.type_index().short_name(), node_state->inputs.size(), inputs.size());
            return false;
        }

        std::vector<std::optional<SlotValue>> outputs(node_state->outputs.size(), std::nullopt);
        {
            GraphContext context(graph, *node_state, inputs, outputs);
            if (view_entity) {
                context.set_view_entity(view_entity.value());
            }
            spdlog::debug("Running node {}.", node_state->label.type_index().short_name());
            node_state->pnode->run(context, render_context, world);

            for (auto&& run_sub_graph : context.finish()) {
                auto sub_graph = graph.get_sub_graph(run_sub_graph.id);
                if (sub_graph) {
                    auto res = run_graph(*sub_graph, run_sub_graph.id, render_context, world, run_sub_graph.inputs,
                                         run_sub_graph.view_entity);
                    if (!res) {
                        spdlog::warn(
                            "Sub graph {} failed to run. Ignoring "
                            "run_sub_graph.",
                            run_sub_graph.id.type_index().short_name());
                        return false;
                    }
                } else {
                    spdlog::warn("Sub graph {} not found. Ignoring run_sub_graph.",
                                 run_sub_graph.id.type_index().short_name());
                    return false;
                }
            }
        }

        std::vector<SlotValue> output_values;
        output_values.reserve(node_state->outputs.size());
        for (auto&& [index, output_slot] : std::views::enumerate(node_state->outputs.iter())) {
            if (index < outputs.size()) {
                if (outputs[index]) {
                    output_values.push_back(*outputs[index]);
                } else {
                    spdlog::warn("Output slot {} is empty. Ignoring run_sub_graph.", output_slot.name);
                    return false;
                }
            }
        }
        node_outputs.emplace(node_state->label, std::move(output_values));

        for (auto&& next_node :
             node_state->edges.output_edges() | std::views::transform([](const Edge& e) { return e.input_node; })) {
            if (auto state = graph.get_node_state(next_node)) {
                node_queue.push_back(state);
            }
        }
    }

    render_context.flush_encoder();  // flush the remaining commandlist. Or maybe just clearState?

    return true;
}