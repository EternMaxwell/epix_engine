#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cstdint>
#include <epix/common.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/task/usages.hpp>
#include <epix/render/graph/decl.hpp>
#include <epix/render/graph/node.hpp>
#include <epix/render/graph/slot.hpp>

namespace epix::render::graph {
struct RunSubGraph {
    GraphLabel id;
    std::vector<SlotValue> inputs;
    std::optional<epix::ecs::Entity> view_entity;
    /** @brief Debug marker name used around the sub-graph run (Bevy
     * RunSubGraph::debug_group, graph_runner.rs:111). */
    std::optional<std::string> debug_group;
};
/**
 * @brief GraphContext provides the context for a node to run in the render graph.
 * It is used to set outputs and get inputs, and to run sub-graphs.
 */
EPIX_EXPORT struct GraphContext {
   private:
    const RenderGraph& m_graph;
    const NodeState& m_node_state;
    std::span<const SlotValue> m_inputs;
    std::span<std::optional<SlotValue>> m_outputs;
    std::vector<RunSubGraph> m_sub_graphs;
    std::optional<epix::ecs::Entity> m_view_entity;

   public:
    /** @brief Construct the context for a node execution.
     * @param graph The parent render graph.
     * @param node_state The state of the node being run.
     * @param inputs Slot values provided as inputs.
     * @param outputs Mutable output slot storage. */
    GraphContext(const RenderGraph& graph,
                 const NodeState& node_state,
                 std::span<const SlotValue> inputs,
                 std::span<std::optional<SlotValue>> outputs) noexcept
        : m_graph(graph), m_node_state(node_state), m_inputs(inputs), m_outputs(outputs) {}
    /** @brief The label of the node being run (Bevy
     * RenderGraphContext::label). */
    const NodeLabel& label() const noexcept { return m_node_state.label; }
    /** @brief Borrow all input slot values as a standard range view. */
    auto inputs() const noexcept { return std::views::all(m_inputs); }
    /** @brief Get the input slot declarations. */
    const SlotInfos& input_info() const noexcept { return m_node_state.inputs; }
    /** @brief Get the output slot declarations. */
    const SlotInfos& output_info() const noexcept { return m_node_state.outputs; }
    /** @brief Get a specific input slot value by label.
     * @return Pointer to the value, or nullptr if not found. */
    const SlotValue* get_input(const SlotLabel& label) const {
        return m_node_state.inputs.get_slot_index(label)
            .transform([&](std::uint32_t index) -> const SlotValue* {
                // Bounds-check like Bevy's panicking get_input: out-of-range
                // indexes return nullptr instead of reading out of bounds.
                if (index >= m_inputs.size()) return nullptr;
                return &m_inputs[index];
            })
            .value_or(nullptr);
    }
    /** @brief Get an Entity-typed input by label. */
    std::optional<epix::ecs::Entity> get_input_entity(const SlotLabel& label) const {
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
    epix::ecs::Entity view_entity() const { return m_view_entity.value(); }
    /** @brief Get the view entity, or std::nullopt if none is set. */
    std::optional<epix::ecs::Entity> get_view_entity() const noexcept { return m_view_entity; }
    /** @brief Assign a view entity to this context. */
    void set_view_entity(epix::ecs::Entity entity) noexcept { m_view_entity = entity; }

    /** @brief Schedule a sub-graph to run after this node finishes.
     * @param label The sub-graph label.
     * @param inputs Input slot values for the sub-graph.
     * @param view_entity Optional view entity to pass to the sub-graph.
     * @return True if the sub-graph was found in the render graph. */
    bool run_sub_graph(const GraphLabel& label,
                       std::span<const SlotValue> inputs,
                       std::optional<epix::ecs::Entity> view_entity = std::nullopt);

    /** @brief Consume and return all queued sub-graph runs. */
    std::vector<RunSubGraph> finish() noexcept { return std::move(m_sub_graphs); }
};
/**
 * @brief RenderContext, stores the wgpu device and command encoder.
 */
EPIX_EXPORT struct RenderContext {
   private:
    /** @brief A command buffer ready for submission, or Bevy's deferred
     * `CommandBufferGenerationTask`. The latter is run by ComputeTaskPool in
     * finish(), after the render graph has stopped borrowing its world. */
    using CommandBufferGenerationTask = std::move_only_function<wgpu::CommandBuffer(wgpu::Device)>;
    using QueuedCommandBuffer         = std::variant<wgpu::CommandBuffer, CommandBufferGenerationTask>;

    wgpu::Device m_device;
    std::optional<wgpu::CommandEncoder> m_command_encoder;
    std::vector<QueuedCommandBuffer> m_queued_commands;

   public:
    /** @brief Construct a render context with the given WebGPU device. */
    RenderContext(wgpu::Device device) noexcept : m_device(std::move(device)) {}

    /** @brief Get the WebGPU device. */
    const wgpu::Device& device() const noexcept { return m_device; }
    /** @brief Get or lazily create the command encoder. */
    wgpu::CommandEncoder& command_encoder() {
        if (!m_command_encoder) {
            m_command_encoder = m_device.createCommandEncoder();
        }
        return *m_command_encoder;
    }
    /** @brief Whether a command encoder has been created yet. */
    bool has_command_encoder() const noexcept { return m_command_encoder.has_value(); }
    /** @brief Begin a new render pass on the current command encoder. */
    wgpu::RenderPassEncoder begin_render_pass(const wgpu::RenderPassDescriptor& desc) {
        return command_encoder().beginRenderPass(desc);
    }
    /** @brief Enqueue an externally-created command buffer.
     * Flushes the open encoder first so previously recorded commands land in
     * the queue BEFORE the added buffer (Bevy renderer/mod.rs:561-569). */
    void add_command_buffer(wgpu::CommandBuffer buffer) {
        flush_encoder();
        m_queued_commands.emplace_back(std::move(buffer));
    }
    /** @brief Queue command-buffer generation work for ComputeTaskPool.
     *
     * Mirrors Bevy `RenderContext::add_command_buffer_generation_task`: any
     * active encoder is flushed first, then all queued tasks run in parallel
     * at finish(). Their results are sorted back into enqueue order before
     * the renderer submits them. */
    template <typename Task>
        requires std::move_constructible<std::remove_cvref_t<Task>> &&
                 std::same_as<std::invoke_result_t<std::remove_cvref_t<Task>&, wgpu::Device>, wgpu::CommandBuffer>
    void add_command_buffer_generation_task(Task&& task) {
        flush_encoder();
        m_queued_commands.emplace_back(CommandBufferGenerationTask(std::forward<Task>(task)));
    }
    /** @brief Finish the current command encoder (if any) and enqueue it. */
    void flush_encoder() {
        if (m_command_encoder) {
            m_queued_commands.emplace_back(m_command_encoder->finish({}));
            m_command_encoder.reset();
        }
    }
    /** @brief Flush and return all command buffers for submission.
     *
     * Deferred generators run on the app ComputeTaskPool where available,
     * matching Bevy's `RenderContext::finish`. Ready command buffers are not
     * delayed, and generated buffers are restored to their original queue
     * positions after the parallel scope completes. */
    std::vector<wgpu::CommandBuffer> finish() && {
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
};

/**
 * @brief A render graph node that runs on a specific view entity (Bevy
 * `ViewNode`). A view node declares a read-only query data type and receives
 * the matching item for the view entity associated with the current graph
 * execution.
 */
EPIX_EXPORT template <typename T>
concept ViewNode = requires {
    typename T::ViewQuery;
} && epix::ecs::readonly_query_data<typename T::ViewQuery> && requires(
    T& node,
    const T& readonly_node,
    GraphContext& graph,
    RenderContext& render_ctx,
    epix::ecs::World& mutable_world,
    const epix::ecs::World& world,
    typename epix::ecs::QueryData<typename T::ViewQuery>::Item view) {
    { node.update(mutable_world) } -> std::same_as<void>;
    { readonly_node.run(graph, render_ctx, view, world) } -> std::same_as<std::expected<void, NodeRunError>>;
};

/**
 * @brief Wraps a `ViewNode` into a regular `Node`, fetching the view entity
 * from the graph context (Bevy `ViewNodeRunner<N>`).
 */
template <ViewNode N>
struct ViewNodeRunner : public Node {
    /** @brief Cached Bevy-style query state for the view entity. It is mutable
     * only because `QueryState::get_manual` is not yet const; `run` never
     * updates archetypes. */
    mutable epix::ecs::QueryState<typename N::ViewQuery> view_query;
    /** @brief The wrapped view node. */
    N node;

    /** @brief Construct a view-node runner and cache its query state (Bevy
     * `ViewNodeRunner::new`). */
    explicit ViewNodeRunner(N value, epix::ecs::World& world)
        : view_query(world.template query<typename N::ViewQuery>()), node(std::move(value)) {}

    /** @brief Update cached archetypes before the node's own per-frame update
     * (Bevy `ViewNodeRunner::update`). */
    void update(epix::ecs::World& world) override {
        view_query.update_archetypes(world);
        node.update(world);
    }

    std::expected<void, NodeRunError> run(GraphContext& graph,
                                          RenderContext& render_ctx,
                                          const epix::ecs::World& world) override {
        // Bevy ViewNodeRunner::run (node.rs:404-425): when the view entity has
        // no matching query item, the node is skipped instead of failing.
        auto view_entity = graph.get_view_entity();
        if (!view_entity) return {};
        auto view = view_query.get_manual(world, *view_entity);
        if (!view) return {};
        return node.run(graph, render_ctx, *view, world);
    }
};

/**
 * @brief Node that runs a named sub-graph on the view entity associated with
 * the current execution (Bevy `RunGraphOnViewNode`).
 */
EPIX_EXPORT struct RunGraphOnViewNode : public Node {
    /** @brief Sub-graph to run. */
    GraphLabel sub_graph;

    explicit RunGraphOnViewNode(const GraphLabel& sub_graph) : sub_graph(sub_graph) {}

    // Bevy RunGraphOnViewNode (node.rs:333-357) declares no slots: the sub
    // graph always runs on the context's view entity, so the node works as a
    // drop-in 'run sub-graph for the current view' node without wiring a slot.
    std::expected<void, NodeRunError> run(GraphContext& graph, RenderContext&, const epix::ecs::World&) override {
        std::vector<SlotValue> inputs{};
        if (!graph.run_sub_graph(sub_graph, inputs, graph.get_view_entity())) {
            return std::unexpected(NodeRunError::RunSubGraphError);
        }
        return {};
    }
};
}  // namespace epix::render::graph
