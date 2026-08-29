
#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <epix/render/graph.hpp>

using namespace epix::render::graph;
using namespace epix::ecs;
using namespace epix::app;

std::expected<void, RenderGraphRunnerError> RenderGraphRunner::run(
    const RenderGraph& graph,
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    World& world,
    std::function<void(wgpu::CommandEncoder&)> finalizer) {
    spdlog::trace("[render.graph] Running render graph.");
    RenderContext render_context(device.clone());
    auto res = run_graph(graph, std::nullopt, render_context, world, {}, std::nullopt);
    if (!res) return std::unexpected(res.error());
    // finalize the command encoder
    if (finalizer) finalizer(render_context.command_encoder());
    // submit generated cmd buffers
    auto command_buffers = std::move(render_context).finish();
    if (command_buffers.size()) queue.submit(command_buffers);
    return {};
}

std::expected<void, RenderGraphRunnerError> RenderGraphRunner::run_graph(const RenderGraph& graph,
                                                                         std::optional<GraphLabel> sub_graph,
                                                                         RenderContext& render_context,
                                                                         World& world,
                                                                         std::span<const SlotValue> inputs,
                                                                         std::optional<Entity> view_entity) {
    // store all outputs of nodes in a map
    std::unordered_map<NodeLabel, std::vector<SlotValue>> node_outputs;

    spdlog::debug("Running graph {}.", sub_graph ? sub_graph->type_index().short_name() : "main");

    auto node_queue = std::ranges::to<std::deque>(std::views::transform(
        std::views::filter(graph.iter_nodes(), [](const NodeState& node) { return node.inputs.empty(); }),
        [](const NodeState& node) { return std::cref(node); }));

    if (auto input_node = graph.get_input_node()) {
        std::vector<SlotValue> input_values;
        for (auto&& [i, input_slot] : std::views::enumerate(input_node->get().inputs.iter())) {
            if (i < inputs.size()) {
                if (input_slot.type != inputs[i].type()) {
                    return std::unexpected(RunnerMismatchedInputSlotType{.slot_index = static_cast<std::size_t>(i),
                                                                         .expected   = input_slot.type,
                                                                         .actual     = inputs[i].type()});
                }
                input_values.push_back(inputs[i]);
            } else {
                return std::unexpected(RunnerMissingInput{
                    .slot_index = static_cast<std::size_t>(i), .slot_name = input_slot.name, .sub_graph = sub_graph});
            }
        }

        node_outputs.emplace(input_node->get().label, std::move(input_values));

        for (auto&& next_node : std::views::transform(input_node->get().edges.output_edges(),
                                                      [](const Edge& e) { return e.input_node; })) {
            if (auto state = graph.get_node_state(next_node)) {
                node_queue.push_front(*state);
            }
        }
    }

    while (!node_queue.empty()) {
        const NodeState& node_state = node_queue.back();
        node_queue.pop_back();

        if (node_outputs.contains(node_state.label)) continue;

        std::vector<std::pair<uint32_t, SlotValue>> slot_indices_and_inputs;
        // check if all dependencies have finished running
        {
            bool break_loop = false;
            for (auto&& [edge, input_node] : std::views::transform(
                     node_state.edges.input_edges(), [](const Edge& e) { return std::pair{e, e.output_node}; })) {
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
        auto inputs = std::ranges::to<std::vector>(
            std::views::transform(slot_indices_and_inputs, [](const auto& pair) { return pair.second; }));
        if (inputs.size() != node_state.inputs.size()) {
            return std::unexpected(RunnerMismatchedInputCount{
                .node = node_state.label, .slot_count = node_state.inputs.size(), .value_count = inputs.size()});
        }

        std::vector<std::optional<SlotValue>> outputs(node_state.outputs.size(), std::nullopt);
        {
            GraphContext context(graph, node_state, inputs, outputs);
            if (view_entity) {
                context.set_view_entity(view_entity.value());
            }
            spdlog::debug("Running node {}.", node_state.label.type_index().short_name());
            if (auto result = node_state.pnode->run(context, render_context, world); !result) {
                return std::unexpected(RunnerNodeRunError{.node = node_state.label, .error = result.error()});
            }

            auto sub_graphs = context.finish();
            for (auto&& run_sub_graph : sub_graphs) {
                auto sub_graph = graph.get_sub_graph(run_sub_graph.id);
                if (sub_graph) {
                    // Bevy graph_runner.rs:284-287: debug marker around each
                    // sub-graph run (trace feature; epix always emits).
                    if (run_sub_graph.debug_group && render_context.has_command_encoder()) {
                        render_context.command_encoder().insertDebugMarker(
                            wgpu::StringView(std::string_view("Start " + *run_sub_graph.debug_group)));
                    }
                    auto res = run_graph(*sub_graph, run_sub_graph.id, render_context, world, run_sub_graph.inputs,
                                         run_sub_graph.view_entity);
                    if (run_sub_graph.debug_group && render_context.has_command_encoder()) {
                        render_context.command_encoder().insertDebugMarker(
                            wgpu::StringView(std::string_view("End " + *run_sub_graph.debug_group)));
                    }
                    if (!res) {
                        return std::unexpected(res.error());
                    }
                } else {
                    return std::unexpected(RunnerSubGraphNotFound{.sub_graph = run_sub_graph.id});
                }
            }
        }

        std::vector<SlotValue> output_values;
        output_values.reserve(node_state.outputs.size());
        for (auto&& [index, output_slot] : std::views::enumerate(node_state.outputs.iter())) {
            if (index < outputs.size()) {
                if (outputs[index]) {
                    output_values.push_back(*outputs[index]);
                } else {
                    return std::unexpected(RunnerEmptyNodeOutputSlot{.node       = node_state.label,
                                                                     .slot_index = static_cast<std::size_t>(index),
                                                                     .slot_name  = output_slot.name});
                }
            }
        }
        node_outputs.emplace(node_state.label, std::move(output_values));

        for (auto&& next_node :
             std::views::transform(node_state.edges.output_edges(), [](const Edge& e) { return e.input_node; })) {
            if (auto state = graph.get_node_state(next_node)) {
                node_queue.push_front(std::ref(*state));
            }
        }
    }

    // Bevy run_graph does NOT flush here: commands accumulate in the shared
    // encoder and are flushed by add_command_buffer / finish() so that
    // externally-added buffers stay in command order (graph_runner.rs).

    return {};
}
