// A visual test for the standard automatic GPU binned-phase API.
//
// This intentionally does not encode a pass/fail color. The graph clears the
// output to a neutral background, then `BinnedRenderPhase::render` invokes a
// real draw function. Compute preprocessing writes both the instance output
// and indirect command counts; the two prepared commands issue triangle draws,
// so a bad preparation/render path leaves geometry missing.

#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace binned_phase_geometry {

struct BatchSetKey {
    int value = 0;
    bool indexed_value = false;

    BatchSetKey() = default;
    BatchSetKey(int value, bool indexed = false) : value(value), indexed_value(indexed) {}
    bool indexed() const noexcept { return indexed_value; }
    auto operator<=>(const BatchSetKey&) const = default;
};

struct Item {
    Entity render_entity{};
    render::phase::DrawFunctionId draw_id{0};
    int bin = 0;
    BatchSetKey batch_set{};
    std::pair<std::uint32_t, std::uint32_t> batch_range{0, 1};
    render::phase::PhaseItemExtraIndex extra{};

    Entity entity() const noexcept { return render_entity; }
    render::sync_world::MainEntity main_entity() const noexcept { return {render_entity}; }
    int sort_key() const noexcept { return bin; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return extra; }
    void set_extra_index(render::phase::PhaseItemExtraIndex value) noexcept { extra = value; }
    using BinKey = int;
    using BatchSetKey = binned_phase_geometry::BatchSetKey;
    const int& bin_key() const noexcept { return bin; }
    const BatchSetKey& batch_set_key() const noexcept { return batch_set; }
    bool batchable() const noexcept { return true; }

    static Item create(BatchSetKey batch_set,
                       int bin,
                       Entity representative,
                       std::uint32_t instance_start,
                       std::uint32_t instance_end) {
        return {.render_entity = representative,
                .draw_id       = render::phase::DrawFunctionId{0},
                .bin           = bin,
                .batch_set     = batch_set,
                .batch_range   = {instance_start, instance_end}};
    }
};

struct Adapter {};

struct PipelineState {
    wgpu::RenderPipeline pipeline;
    wgpu::ComputePipeline preprocess_pipeline;
    wgpu::BindGroup render_bind_group;
    wgpu::BindGroup preprocess_bind_group;
    wgpu::BindGroupLayout render_bind_group_layout;
    wgpu::Buffer vertex_buffer;
    wgpu::Buffer input_indices_buffer;
    wgpu::Buffer work_items_buffer;
    wgpu::Buffer output_indices_buffer;
    wgpu::Buffer indirect_parameters_buffer;
    wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
};

struct TriangleDraw : render::phase::DrawFunction<Item> {
    std::shared_ptr<PipelineState> state;

    explicit TriangleDraw(std::shared_ptr<PipelineState> state) : state(std::move(state)) {}
    void prepare(const World&) override {}
    std::expected<void, render::phase::DrawError> draw(const World&,
                                                       const wgpu::RenderPassEncoder& pass,
                                                       Entity,
                                                       const Item& item) override {
        if (!state->pipeline) return std::unexpected(render::phase::DrawError::skip());
        pass.setPipeline(state->pipeline);
        pass.setBindGroup(0, state->render_bind_group, std::span<const std::uint32_t>{});
        pass.setVertexBuffer(0, state->vertex_buffer, 0, 3 * sizeof(float) * 2);
        if (item.extra.type == render::phase::PhaseItemExtraIndex::Type::IndirectParametersIndex) {
            for (auto command = item.extra.indirect_range.first; command < item.extra.indirect_range.second; ++command) {
                pass.drawIndirect(state->indirect_parameters_buffer,
                                  command * sizeof(render::batching::IndirectParametersNonIndexed));
            }
        } else {
            pass.draw(3, item.batch_range.second - item.batch_range.first, 0, item.batch_range.first);
        }
        return {};
    }
};

}  // namespace binned_phase_geometry

template <>
struct std::hash<binned_phase_geometry::BatchSetKey> {
    std::size_t operator()(const binned_phase_geometry::BatchSetKey& key) const noexcept {
        return std::hash<int>{}(key.value) ^ (std::hash<bool>{}(key.indexed_value) << 1);
    }
};

template <>
struct render::batching::GetBatchData<binned_phase_geometry::Adapter> {
    using Param = World&;
    using CompareData = std::uint32_t;
    using BufferData = std::uint32_t;

    std::optional<std::pair<BufferData, std::optional<CompareData>>> get_batch_data(
        World&, std::pair<Entity, render::sync_world::MainEntity>) const {
        return std::nullopt;
    }
};

template <>
struct render::batching::GetFullBatchData<binned_phase_geometry::Adapter> {
    using BufferInputData = std::uint32_t;

    std::optional<std::uint32_t> get_binned_batch_data(World&, render::sync_world::MainEntity entity) const {
        return entity.entity.index;
    }
    std::optional<std::pair<std::uint32_t, std::optional<std::uint32_t>>> get_index_and_compare_data(
        World&, render::sync_world::MainEntity entity) const {
        return std::pair{entity.entity.index, std::optional{entity.entity.index}};
    }
    std::optional<std::uint32_t> get_binned_index(World&, render::sync_world::MainEntity entity) const {
        return entity.entity.index;
    }
    void write_batch_indirect_parameters_metadata(bool,
                                                  std::uint32_t output_index,
                                                  std::optional<std::uint32_t>,
                                                  render::batching::UntypedPhaseIndirectParametersBuffers& buffers,
                                                  std::uint32_t command_index) const {
        buffers.non_indexed_data.values.at(command_index) = {
            .vertex_count = 3, .instance_count = 0, .first_vertex = 0, .first_instance = output_index};
        buffers.set_cpu_metadata(false, command_index, {.base_output_index = output_index, .batch_set_index = 0});
    }
};

namespace {

using binned_phase_geometry::Adapter;
using binned_phase_geometry::Item;

static_assert(render::phase::BinnedPhaseItem<Item>);
static_assert(render::batching::GetFullBatchDataImpl<Adapter>);

// The engine's pipeline assets use Slang. This deliberately tiny direct-wgpu
// shader is test scaffolding for the accepted direct-resource layer: its
// compute stage consumes the generic GPU-preprocessing work items and its
// vertex stage consumes the generated output data.
constexpr std::string_view kPreprocessShader = R"(
struct PreprocessWorkItem {
    input_index: u32,
    output_or_indirect_parameters_index: u32,
};
struct IndirectMetadata {
    base_output_index: u32,
    batch_set_index: u32,
};
struct IndirectParameters {
    vertex_count: u32,
    instance_count: atomic<u32>,
    first_vertex: u32,
    first_instance: u32,
};

@group(0) @binding(0) var<storage, read> input_indices: array<u32>;
@group(0) @binding(1) var<storage, read> work_items: array<PreprocessWorkItem>;
@group(0) @binding(2) var<storage, read_write> output_indices: array<u32>;
@group(0) @binding(3) var<storage, read> indirect_metadata: array<IndirectMetadata>;
@group(0) @binding(4) var<storage, read_write> indirect_parameters: array<IndirectParameters>;

@compute @workgroup_size(64)
fn preprocessMain(@builtin(global_invocation_id) invocation: vec3<u32>) {
    if (invocation.x >= arrayLength(&work_items)) {
        return;
    }
    let item = work_items[invocation.x];
    let command_index = item.output_or_indirect_parameters_index;
    let output_index = indirect_metadata[command_index].base_output_index +
        atomicAdd(&indirect_parameters[command_index].instance_count, 1u);
    output_indices[output_index] = input_indices[item.input_index];
}
)";

constexpr std::string_view kTriangleShader = R"(
@group(0) @binding(2) var<storage, read> output_indices: array<u32>;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @builtin(instance_index) instance_index: u32,
};

@vertex
fn vertexMain(input: VertexInput) -> @builtin(position) vec4<f32> {
    let processed_index = output_indices[input.instance_index];
    let column = processed_index % 3u;
    let row = processed_index / 3u;
    let center = vec2<f32>(-0.62 + f32(column) * 0.62, 0.34 - f32(row) * 0.62);
    return vec4<f32>(center + input.position, 0.0, 1.0);
}

@fragment
fn fragmentMain() -> @location(0) vec4<f32> {
    return vec4<f32>(0.98, 0.69, 0.20, 1.0);
}
)";

constexpr struct BinnedGeometryGraphLabel {
} kBinnedGeometryGraph;

struct BinnedGeometryNode : render::graph::Node {
    std::optional<QueryState<epix::ecs::Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>> views;
    std::shared_ptr<binned_phase_geometry::PipelineState> pipeline = std::make_shared<binned_phase_geometry::PipelineState>();
    render::phase::BinnedRenderPhase<Item> phase;
    wgpu::ShaderModule shader_module;
    wgpu::ShaderModule preprocess_shader_module;
    std::uint32_t preprocess_work_item_count = 0;
    bool prepared = false;

    void update(World& world) override {
        if (!views) {
            views = world.try_query<epix::ecs::Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>();
        } else {
            views->update_archetypes(world);
        }
        if (prepared) return;

        render::phase::DrawFunctions<Item> draw_functions;
        const auto draw_id = draw_functions.add<binned_phase_geometry::TriangleDraw>(pipeline);
        if (draw_id != render::phase::DrawFunctionId{0}) throw std::runtime_error("unexpected first draw-function id");
        world.insert_resource(std::move(draw_functions));

        const Tick tick{1};
        render::batching::UntypedPhaseBatchedInstanceBuffers<std::uint32_t> phase_buffers;
        render::batching::UntypedPhaseIndirectParametersBuffers indirect_parameters;
        render::batching::InstanceInputUniformBuffer<std::uint32_t> input_indices;
        for (std::uint32_t index = 0; index < 5; ++index) input_indices.add(index);
        input_indices.ensure_nonempty();
        const render::view::RetainedViewEntity retained_view{
            render::sync_world::MainEntity{Entity::from_index(1)}, std::nullopt, 0};
        phase = render::phase::BinnedRenderPhase<Item>{render::batching::GpuPreprocessingMode::Culling};
        for (std::uint32_t index = 0; index < 3; ++index) {
            phase.add(0, 0, Entity::from_index(100 + index), render::sync_world::MainEntity{Entity::from_index(index + 1)},
                      render::phase::InputUniformIndex{index}, render::phase::BinnedRenderPhaseType::MultidrawableMesh, tick);
        }
        for (std::uint32_t index = 0; index < 2; ++index) {
            phase.add(0, 1, Entity::from_index(200 + index), render::sync_world::MainEntity{Entity::from_index(index + 4)},
                      render::phase::InputUniformIndex{index + 3}, render::phase::BinnedRenderPhaseType::MultidrawableMesh, tick);
        }
        render::batching::batch_and_prepare_gpu_binned_phase<Item, Adapter>(
            phase, phase_buffers, indirect_parameters, retained_view, false, false, world);

        auto device = world.get_resource<wgpu::Device>();
        if (!device) throw std::runtime_error("render device is unavailable");
        constexpr std::array<float, 6> triangle_vertices = {0.0f, 0.16f, -0.14f, -0.12f, 0.14f, -0.12f};
        pipeline->vertex_buffer = device->get().createBuffer(
            wgpu::BufferDescriptor()
                .setLabel("binned-phase-geometry-vertices")
                .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst)
                .setSize(sizeof(triangle_vertices)));
        world.resource<wgpu::Queue>().writeBuffer(pipeline->vertex_buffer, 0, triangle_vertices.data(),
                                                   sizeof(triangle_vertices));
        shader_module = device->get().createShaderModule(
            wgpu::ShaderModuleDescriptor()
                .setLabel("binned-phase-geometry-test-shader")
                .setNextInChain(wgpu::ShaderSourceWGSL().setCode(kTriangleShader)));
        preprocess_shader_module = device->get().createShaderModule(
            wgpu::ShaderModuleDescriptor()
                .setLabel("binned-phase-geometry-preprocess-shader")
                .setNextInChain(wgpu::ShaderSourceWGSL().setCode(kPreprocessShader)));
        if (!shader_module || !preprocess_shader_module) {
            throw std::runtime_error("failed to create binned-phase test shader modules");
        }
        input_indices.buffer.write_buffer(device->get(), world.resource<wgpu::Queue>());
        phase_buffers.write_buffers(device->get(), world.resource<wgpu::Queue>());
        indirect_parameters.write_buffers(device->get(), world.resource<wgpu::Queue>());
        const auto& work_items = std::get<render::batching::PreprocessWorkItemBuffers::Indirect>(
            phase_buffers.work_item_buffers.at(retained_view).storage).non_indexed;
        preprocess_work_item_count = static_cast<std::uint32_t>(work_items.len());
        pipeline->input_indices_buffer = input_indices.buffer.buffer;
        pipeline->work_items_buffer = work_items.buffer;
        pipeline->output_indices_buffer = phase_buffers.data_buffer.buffer;
        pipeline->indirect_parameters_buffer = indirect_parameters.non_indexed_data.buffer;
        const auto preprocess_layout = device->get().createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("binned-phase-geometry-preprocess-layout")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry().setBinding(0).setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry().setBinding(1).setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry().setBinding(2).setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eStorage)),
                    wgpu::BindGroupLayoutEntry().setBinding(3).setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry().setBinding(4).setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eStorage)),
                }));
        pipeline->render_bind_group_layout = device->get().createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("binned-phase-geometry-render-layout")
                .setEntries(std::array{wgpu::BindGroupLayoutEntry()
                                            .setBinding(2)
                                            .setVisibility(wgpu::ShaderStage::eVertex)
                                            .setBuffer(wgpu::BufferBindingLayout()
                                                           .setType(wgpu::BufferBindingType::eReadOnlyStorage))}));
        const auto preprocess_pipeline_layout = device->get().createPipelineLayout(
            wgpu::PipelineLayoutDescriptor().setLabel("binned-phase-geometry-preprocess-pipeline-layout")
                .setBindGroupLayouts(std::array{preprocess_layout}));
        pipeline->preprocess_pipeline = device->get().createComputePipeline(
            wgpu::ComputePipelineDescriptor().setLabel("binned-phase-geometry-preprocess-pipeline")
                .setLayout(preprocess_pipeline_layout)
                .setCompute(wgpu::ProgrammableStageDescriptor().setModule(preprocess_shader_module).setEntryPoint("preprocessMain")));
        pipeline->preprocess_bind_group = device->get().createBindGroup(
            wgpu::BindGroupDescriptor().setLabel("binned-phase-geometry-preprocess-bind-group")
                .setLayout(preprocess_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(pipeline->input_indices_buffer)
                        .setSize(input_indices.buffer.len() * sizeof(std::uint32_t)),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(pipeline->work_items_buffer)
                        .setSize(work_items.len() * sizeof(render::batching::PreprocessWorkItem)),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(pipeline->output_indices_buffer)
                        .setSize(phase_buffers.data_buffer.len() * sizeof(std::uint32_t)),
                    wgpu::BindGroupEntry().setBinding(3).setBuffer(indirect_parameters.non_indexed_cpu_metadata.buffer)
                        .setSize(indirect_parameters.non_indexed_cpu_metadata.len() *
                                 sizeof(render::batching::IndirectParametersCpuMetadata)),
                    wgpu::BindGroupEntry().setBinding(4).setBuffer(pipeline->indirect_parameters_buffer)
                        .setSize(indirect_parameters.non_indexed_data.len() *
                                 sizeof(render::batching::IndirectParametersNonIndexed)),
                }));
        pipeline->render_bind_group = device->get().createBindGroup(
            wgpu::BindGroupDescriptor().setLabel("binned-phase-geometry-render-bind-group")
                .setLayout(pipeline->render_bind_group_layout)
                .setEntries(std::array{wgpu::BindGroupEntry().setBinding(2).setBuffer(pipeline->output_indices_buffer)
                                            .setSize(phase_buffers.data_buffer.len() * sizeof(std::uint32_t))}));
        prepared = true;
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& context,
                                                          render::graph::RenderContext& render_context,
                                                          const World& world) override {
        if (!views || !shader_module || !preprocess_shader_module) return {};
        const auto view = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(context.view_entity());
        if (!view) return {};
        const auto& target = std::get<1>(*view);
        if (!pipeline->pipeline || pipeline->format != target.output_attachment.view_format) {
            const auto format = target.output_attachment.view_format;
            auto layout = world.resource<wgpu::Device>().createPipelineLayout(
                wgpu::PipelineLayoutDescriptor().setLabel("binned-phase-geometry-layout")
                    .setBindGroupLayouts(std::array{pipeline->render_bind_group_layout}));
            const auto attributes = std::array{wgpu::VertexAttribute()
                                                   .setFormat(wgpu::VertexFormat::eFloat32x2)
                                                   .setOffset(0)
                                                   .setShaderLocation(0)};
            const auto vertex_buffers = std::array{wgpu::VertexBufferLayout()
                                                        .setArrayStride(sizeof(float) * 2)
                                                        .setStepMode(wgpu::VertexStepMode::eVertex)
                                                        .setAttributes(attributes)};
            pipeline->pipeline = world.resource<wgpu::Device>().createRenderPipeline(
                wgpu::RenderPipelineDescriptor()
                    .setLabel("binned-phase-geometry-pipeline")
                    .setLayout(layout)
                    .setVertex(wgpu::VertexState().setModule(shader_module).setEntryPoint("vertexMain").setBuffers(vertex_buffers))
                    .setFragment(wgpu::FragmentState()
                                     .setModule(shader_module)
                                     .setEntryPoint("fragmentMain")
                                     .setTargets(std::array{wgpu::ColorTargetState()
                                                                .setFormat(format)
                                                                .setWriteMask(wgpu::ColorWriteMask::eAll)}))
                    .setPrimitive(wgpu::PrimitiveState()
                                      .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                                      .setFrontFace(wgpu::FrontFace::eCCW)
                                      .setCullMode(wgpu::CullMode::eNone))
                    .setMultisample(wgpu::MultisampleState().setCount(1).setMask(~0u)));
            pipeline->format = format;
        }
        {
            auto preprocess = render_context.command_encoder().beginComputePass(
                wgpu::ComputePassDescriptor().setLabel("binned-phase-geometry-preprocess-pass"));
            preprocess.setPipeline(pipeline->preprocess_pipeline);
            preprocess.setBindGroup(0, pipeline->preprocess_bind_group, std::span<const std::uint32_t>{});
            preprocess.dispatchWorkgroups((preprocess_work_item_count + 63) / 64, 1, 1);
            preprocess.end();
        }
        auto pass = render_context.command_encoder().beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(
            std::array{target.out_texture_color_attachment(glm::vec4{0.10f, 0.11f, 0.14f, 1.0f})}));
        phase.render(pass, world, context.view_entity());
        pass.end();
        render_context.flush_encoder();
        return {};
    }
};

struct BinnedGeometryPlugin {
    void attach(App& app) {
        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) return;
        render::graph::RenderGraph graph;
        constexpr struct BinnedGeometryNodeLabel {
        } kBinnedGeometryNode;
        graph.add_node(render::graph::NodeLabel(kBinnedGeometryNode), BinnedGeometryNode{});
        render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
            render::graph::GraphLabel(kBinnedGeometryGraph), std::move(graph));
    }
};

}  // namespace

int main() {
    App app = App::create();
    window::Window primary_window;
    primary_window.title = "Binned Phase Geometry Test";
    primary_window.size = {1280, 720};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                           .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(BinnedGeometryPlugin{});

    app.add_systems(Startup, into([](Commands commands) {
                        commands.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{},
                                       render::camera::CameraRenderGraph(kBinnedGeometryGraph), transform::Transform{});
                    }));
    app.run();
}
