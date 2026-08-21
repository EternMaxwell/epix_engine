#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

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
    const std::vector<SlotValue>& m_inputs;
    std::vector<std::optional<SlotValue>>& m_outputs;
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
                 const std::vector<SlotValue>& inputs,
                 std::vector<std::optional<SlotValue>>& outputs) noexcept
        : m_graph(graph), m_node_state(node_state), m_inputs(inputs), m_outputs(outputs) {}
    /** @brief The label of the node being run (Bevy
     * RenderGraphContext::label). */
    const NodeLabel& label() const noexcept { return m_node_state.label; }
    /** @brief Get all input slot values. */
    const std::vector<SlotValue>& inputs() const noexcept { return m_inputs; }
    /** @brief Get the input slot declarations. */
    const SlotInfos& input_info() const noexcept { return m_node_state.inputs; }
    /** @brief Get the output slot declarations. */
    const SlotInfos& output_info() const noexcept { return m_node_state.outputs; }
    /** @brief Get a specific input slot value by label.
     * @return Pointer to the value, or nullptr if not found. */
    const SlotValue* get_input(const SlotLabel& label) const {
        return m_node_state.inputs.get_slot_index(label)
            .transform([&](std::uint32_t index) -> const SlotValue* { return &m_inputs[index]; })
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
    wgpu::Device m_device;
    std::optional<wgpu::CommandEncoder> m_command_encoder;
    std::vector<wgpu::CommandBuffer> m_queued_commands;

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

/**
 * @brief Error type returned by a node run in Bevy (`NodeRunError`). The C++
 * graph runner ignores return values (nodes are `void`), so this type exists
 * for parity and for nodes that want to signal failures explicitly.
 */
EPIX_EXPORT enum class NodeRunError {
    /** @brief The node could not run because it has unconnected inputs. */
    InvalidInputs,
    /** @brief The node ran but produced invalid output. */
    InvalidOutputs,
    /** @brief A requested sub-graph does not exist. */
    SubGraphDoesNotExist,
};

/**
 * @brief A render graph node that runs on a specific view entity (Bevy
 * `ViewNode`). The `run` receives the view entity associated with the
 * current graph execution.
 */
EPIX_EXPORT template <typename T>
concept ViewNode = std::derived_from<T, Node> && requires(T& node,
                                                          GraphContext& graph,
                                                          RenderContext& render_ctx,
                                                          epix::ecs::World& world,
                                                          epix::ecs::Entity view_entity) {
    { node.run(graph, render_ctx, world, view_entity) };
    { node.update(world) };
};

/**
 * @brief Wraps a `ViewNode` into a regular `Node`, fetching the view entity
 * from the graph context (Bevy `ViewNodeRunner<N>`).
 */
template <ViewNode N>
struct ViewNodeRunner : public Node {
    /** @brief The wrapped view node. */
    N node;

    template <typename... Args>
    explicit ViewNodeRunner(Args&&... args) : node(std::forward<Args>(args)...) {}

    std::vector<SlotInfo> input() override { return node.input(); }
    std::vector<SlotInfo> output() override { return node.output(); }
    void update(epix::ecs::World& world) override { node.update(world); }

    void run(GraphContext& graph, RenderContext& render_ctx, const epix::ecs::World& world) override {
        // Bevy ViewNodeRunner::run (node.rs:413-425): when the view entity has
        // no matching component (here: the view entity is unset or does not
        // exist), the node is skipped instead of failing.
        auto view_entity = graph.get_view_entity();
        if (!view_entity) return;
        if (!world.get_entity(*view_entity).has_value()) return;
        node.run(graph, render_ctx, world, *view_entity);
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

    std::vector<SlotInfo> input() override {
        return std::vector<SlotInfo>{SlotInfo{"view", SlotType::Entity}};
    }

    void run(GraphContext& graph, RenderContext&, const epix::ecs::World&) override {
        auto view_entity = graph.get_input_entity("view");
        std::vector<SlotValue> inputs{};
        graph.run_sub_graph(sub_graph, inputs, view_entity);
    }
};
}  // namespace epix::render::graph