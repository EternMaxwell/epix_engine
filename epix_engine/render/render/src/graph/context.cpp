
#include <spdlog/spdlog.h>

#include <iterator>

#include <epix/render.hpp>
#include <epix/render/graph.hpp>

using namespace epix::render::graph;
using namespace epix::ecs;
using namespace epix::app;

bool GraphContext::run_sub_graph(const GraphLabel& graph,
                                 std::span<const SlotValue> inputs,
                                 std::optional<Entity> view_entity) {
    auto sub_graph = m_graph.get_sub_graph(graph);
    if (sub_graph) {
        // check the inputs matches the sub graph inputs
        if (auto input_node = sub_graph->get().get_input_node()) {
            size_t required_inputs_size = input_node->get().inputs.size();
            size_t inputs_size          = inputs.size();
            if (required_inputs_size != inputs_size) {
                spdlog::warn("[run_sub_graph] Sub graph {} has {} inputs, but {} inputs were provided.",
                             graph.type_index().short_name(), required_inputs_size, inputs_size);
                return false;
            }
            for (size_t i = 0; i < required_inputs_size; i++) {
                auto input = input_node->get().inputs.get_slot((uint32_t)i);
                if (input) {
                    auto value = *(inputs.begin() + i);
                    if (input->get().type != value.type()) {
                        spdlog::warn("[run_sub_graph] Sub graph {} input {} type mismatch. Expected {}, got {}.",
                                     graph.type_index().short_name(), input->get().name, type_name(input->get().type),
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

std::vector<wgpu::CommandBuffer> RenderContext::finish() && {
    flush_encoder();

    std::vector<std::pair<std::size_t, wgpu::CommandBuffer>> ordered_buffers;
    ordered_buffers.reserve(m_queued_commands.size());
    bool has_generation_tasks = false;
    for (const auto& queued : m_queued_commands) {
        has_generation_tasks |= std::holds_alternative<CommandBufferGenerationTask>(queued);
    }

    std::vector<std::pair<std::size_t, wgpu::CommandBuffer>> generated_buffers;
    if (has_generation_tasks) {
        generated_buffers = epix::task::ComputeTaskPool::get().scope<std::pair<std::size_t, wgpu::CommandBuffer>>(
            [this, &ordered_buffers](epix::task::Scope<std::pair<std::size_t, wgpu::CommandBuffer>>& scope) {
                for (auto&& [index, queued] : std::views::enumerate(m_queued_commands)) {
                    std::visit(
                        [&]<typename T>(T&& value) {
                            using Value = std::remove_cvref_t<T>;
                            if constexpr (std::same_as<Value, wgpu::CommandBuffer>) {
                                ordered_buffers.emplace_back(index, std::move(value));
                            } else {
                                auto device = m_device.clone();
                                scope.spawn([index, device = std::move(device), task = std::move(value)]() mutable {
                                    return std::pair{index, std::move(task)(std::move(device))};
                                });
                            }
                        },
                        std::move(queued));
                }
            });
    } else {
        for (auto&& [index, queued] : std::views::enumerate(m_queued_commands)) {
            ordered_buffers.emplace_back(index, std::get<wgpu::CommandBuffer>(std::move(queued)));
        }
    }

    ordered_buffers.insert(ordered_buffers.end(), std::make_move_iterator(generated_buffers.begin()),
                           std::make_move_iterator(generated_buffers.end()));
    std::ranges::sort(ordered_buffers, {}, &std::pair<std::size_t, wgpu::CommandBuffer>::first);
    return ordered_buffers | std::views::values | std::ranges::to<std::vector<wgpu::CommandBuffer>>();
}
