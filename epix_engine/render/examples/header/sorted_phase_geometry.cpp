// A visual test for GPU preprocessing of a sorted render phase. It has no
// pass/fail surface: compute writes the instance output used by the vertex
// stage, and RenderPhase consumes the prepared ranges to draw real geometry.

#include <array>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <memory>
#include <optional>
#include <utility>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace sorted_phase_geometry {

struct Item {
    Entity render_entity{};
    // MainEntity intentionally has no default constructor. A placeholder keeps
    // this visual-test phase item aggregate-initializable until it is populated
    // with the corresponding main-world entity below.
    render::sync_world::MainEntity main{Entity::PLACEHOLDER};
    render::CachedPipelineId pipeline_id{};
    render::phase::DrawFunctionId draw_id{};
    std::pair<std::uint32_t, std::uint32_t> batch_range{0, 1};
    render::phase::PhaseItemExtraIndex extra{};

    Entity entity() const noexcept { return render_entity; }
    render::sync_world::MainEntity main_entity() const noexcept { return main; }
    std::uint32_t sort_key() const noexcept { return render_entity.index; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_id; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    bool indexed() const noexcept { return false; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return extra; }
    void set_extra_index(render::phase::PhaseItemExtraIndex value) noexcept { extra = value; }
};

struct Adapter {};

struct PipelineState {
    wgpu::RenderPipeline render_pipeline;
    wgpu::ComputePipeline preprocess_pipeline;
    wgpu::BindGroup render_bind_group;
    wgpu::BindGroup preprocess_bind_group;
    wgpu::BindGroupLayout render_bind_group_layout;
    wgpu::Buffer vertex_buffer;
    wgpu::Buffer input_indices_buffer;
    wgpu::Buffer work_items_buffer;
    wgpu::Buffer output_indices_buffer;
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
        if (!state->render_pipeline) return std::unexpected(render::phase::DrawError::skip());
        pass.setPipeline(state->render_pipeline);
        pass.setBindGroup(0, state->render_bind_group, std::span<const std::uint32_t>{});
        pass.setVertexBuffer(0, state->vertex_buffer, 0, 6 * sizeof(float));
        pass.draw(3, item.batch_range.second - item.batch_range.first, 0, item.batch_range.first);
        return {};
    }
};

}  // namespace sorted_phase_geometry

template <>
struct render::batching::GetBatchData<sorted_phase_geometry::Adapter> {
    using Param       = World&;
    using CompareData = std::uint32_t;
    using BufferData  = std::uint32_t;
    std::optional<std::pair<BufferData, std::optional<CompareData>>> get_batch_data(
        World&, std::pair<Entity, render::sync_world::MainEntity>) const {
        return std::nullopt;
    }
};

template <>
struct render::batching::GetFullBatchData<sorted_phase_geometry::Adapter> {
    using BufferInputData = std::uint32_t;
    std::optional<std::uint32_t> get_binned_batch_data(World&, render::sync_world::MainEntity entity) const {
        return entity.id().index;
    }
    std::optional<std::pair<std::uint32_t, std::optional<std::uint32_t>>> get_index_and_compare_data(
        World&, render::sync_world::MainEntity entity) const {
        return std::pair{entity.id().index, entity.id().index < 3 ? std::optional{1u} : std::optional{2u}};
    }
    std::optional<std::uint32_t> get_binned_index(World&, render::sync_world::MainEntity entity) const {
        return entity.id().index;
    }
    void write_batch_indirect_parameters_metadata(bool,
                                                  std::uint32_t,
                                                  std::optional<std::uint32_t>,
                                                  render::batching::UntypedPhaseIndirectParametersBuffers&,
                                                  std::uint32_t) const {}
};

namespace {

using sorted_phase_geometry::Adapter;
using sorted_phase_geometry::Item;

static_assert(render::phase::SortedPhaseItem<Item>);
static_assert(render::batching::GetFullBatchDataImpl<Adapter>);

// Direct-wgpu shader scaffolding is intentional under Epix's accepted
// resource-layer divergence. Keeping compute and vertex modules separate
// permits a writable output binding in compute and a readonly one in vertex.
constexpr std::string_view kPreprocessShader = R"(
struct WorkItem { input_index: u32, output_index: u32, };
@group(0) @binding(0) var<storage, read> input_indices: array<u32>;
@group(0) @binding(1) var<storage, read> work_items: array<WorkItem>;
@group(0) @binding(2) var<storage, read_write> output_indices: array<u32>;
@compute @workgroup_size(64)
fn preprocessMain(@builtin(global_invocation_id) invocation: vec3<u32>) {
    if (invocation.x >= arrayLength(&work_items)) { return; }
    let item = work_items[invocation.x];
    output_indices[item.output_index] = input_indices[item.input_index];
}
)";

constexpr std::string_view kRenderShader = R"(
@group(0) @binding(2) var<storage, read> output_indices: array<u32>;
struct VertexInput { @location(0) position: vec2<f32>, @builtin(instance_index) instance_index: u32, };
@vertex fn vertexMain(input: VertexInput) -> @builtin(position) vec4<f32> {
    let processed = output_indices[input.instance_index];
    let column = processed % 3u;
    let row = processed / 3u;
    let center = vec2<f32>(-0.62 + f32(column) * 0.62, 0.34 - f32(row) * 0.62);
    return vec4<f32>(center + input.position, 0.0, 1.0);
}
@fragment fn fragmentMain() -> @location(0) vec4<f32> { return vec4<f32>(0.25, 0.76, 0.94, 1.0); }
)";

constexpr struct SortedGeometryGraphLabel {
} kSortedGeometryGraph;

struct SortedGeometryNode : render::graph::Node {
    std::optional<QueryState<ecs::Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>> views;
    std::shared_ptr<sorted_phase_geometry::PipelineState> pipeline =
        std::make_shared<sorted_phase_geometry::PipelineState>();
    render::phase::SortedRenderPhase<Item> phase;
    wgpu::ShaderModule render_shader;
    wgpu::ShaderModule preprocess_shader;
    std::uint32_t work_item_count = 0;
    bool prepared                 = false;

    void update(World& world) override {
        if (!views)
            views =
                world.try_query<ecs::Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>();
        else
            views->update_archetypes(world);
        if (prepared) return;

        render::phase::DrawFunctions<Item> draw_functions;
        const auto draw_id = draw_functions.add<sorted_phase_geometry::TriangleDraw>(pipeline);
        if (draw_id != render::phase::DrawFunctionId{0}) throw std::runtime_error("unexpected first draw-function id");
        world.insert_resource(std::move(draw_functions));

        for (std::uint32_t index = 0; index < 5; ++index) {
            phase.items.push_back({.render_entity = Entity::from_index(100 + index),
                                   .main          = render::sync_world::MainEntity{Entity::from_index(index)},
                                   .pipeline_id   = render::CachedPipelineId{1},
                                   .draw_id       = draw_id});
        }
        render::batching::UntypedPhaseBatchedInstanceBuffers<std::uint32_t> phase_buffers;
        render::batching::UntypedPhaseIndirectParametersBuffers indirect_parameters;
        render::batching::InstanceInputUniformBuffer<std::uint32_t> inputs;
        for (std::uint32_t index = 0; index < 5; ++index) inputs.add(index);
        inputs.ensure_nonempty();
        const render::view::RetainedViewEntity retained_view{render::sync_world::MainEntity{Entity::from_index(1)},
                                                             std::nullopt, 0};
        render::batching::batch_and_prepare_gpu_sorted_phase<Item, Adapter>(phase, phase_buffers, indirect_parameters,
                                                                            retained_view, true, false, world);

        auto device = world.get_resource<wgpu::Device>();
        if (!device) throw std::runtime_error("render device is unavailable");
        constexpr std::array<float, 6> vertices = {0.0f, 0.16f, -0.14f, -0.12f, 0.14f, -0.12f};
        pipeline->vertex_buffer =
            device->get().createBuffer(wgpu::BufferDescriptor()
                                           .setLabel("sorted-phase-geometry-vertices")
                                           .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst)
                                           .setSize(sizeof(vertices)));
        world.resource<wgpu::Queue>().writeBuffer(pipeline->vertex_buffer, 0, vertices.data(), sizeof(vertices));
        render_shader =
            device->get().createShaderModule(wgpu::ShaderModuleDescriptor()
                                                 .setLabel("sorted-phase-geometry-render-shader")
                                                 .setNextInChain(wgpu::ShaderSourceWGSL().setCode(kRenderShader)));
        preprocess_shader =
            device->get().createShaderModule(wgpu::ShaderModuleDescriptor()
                                                 .setLabel("sorted-phase-geometry-preprocess-shader")
                                                 .setNextInChain(wgpu::ShaderSourceWGSL().setCode(kPreprocessShader)));
        if (!render_shader || !preprocess_shader)
            throw std::runtime_error("failed to create sorted-phase test shaders");
        inputs.buffer.write_buffer(device->get(), world.resource<wgpu::Queue>());
        phase_buffers.write_buffers(device->get(), world.resource<wgpu::Queue>());
        const auto& work_items          = std::get<render::batching::PreprocessWorkItemBuffers::Direct>(
                                              phase_buffers.work_item_buffers.at(retained_view).storage)
                                              .items;
        work_item_count                 = static_cast<std::uint32_t>(work_items.len());
        pipeline->input_indices_buffer  = *inputs.buffer.buffer();
        pipeline->work_items_buffer     = *work_items.buffer();
        pipeline->output_indices_buffer = *phase_buffers.data_buffer.buffer();
        const auto preprocess_layout    = device->get().createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("sorted-phase-geometry-preprocess-layout")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(2)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eStorage)),
                }));
        pipeline->render_bind_group_layout = device->get().createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("sorted-phase-geometry-render-layout")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(2)
                        .setVisibility(wgpu::ShaderStage::eVertex)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage))}));
        auto preprocess_pipeline_layout =
            device->get().createPipelineLayout(wgpu::PipelineLayoutDescriptor()
                                                   .setLabel("sorted-phase-geometry-preprocess-pipeline-layout")
                                                   .setBindGroupLayouts(std::array{preprocess_layout}));
        pipeline->preprocess_pipeline = device->get().createComputePipeline(
            wgpu::ComputePipelineDescriptor()
                .setLabel("sorted-phase-geometry-preprocess-pipeline")
                .setLayout(preprocess_pipeline_layout)
                .setCompute(
                    wgpu::ProgrammableStageDescriptor().setModule(preprocess_shader).setEntryPoint("preprocessMain")));
        pipeline->preprocess_bind_group = device->get().createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("sorted-phase-geometry-preprocess-bind-group")
                .setLayout(preprocess_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry()
                        .setBinding(0)
                        .setBuffer(pipeline->input_indices_buffer)
                        .setSize(inputs.buffer.len() * sizeof(std::uint32_t)),
                    wgpu::BindGroupEntry()
                        .setBinding(1)
                        .setBuffer(pipeline->work_items_buffer)
                        .setSize(work_items.len() * sizeof(render::batching::PreprocessWorkItem)),
                    wgpu::BindGroupEntry()
                        .setBinding(2)
                        .setBuffer(pipeline->output_indices_buffer)
                        .setSize(phase_buffers.data_buffer.len() * sizeof(std::uint32_t)),
                }));
        pipeline->render_bind_group = device->get().createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("sorted-phase-geometry-render-bind-group")
                .setLayout(pipeline->render_bind_group_layout)
                .setEntries(std::array{wgpu::BindGroupEntry()
                                           .setBinding(2)
                                           .setBuffer(pipeline->output_indices_buffer)
                                           .setSize(phase_buffers.data_buffer.len() * sizeof(std::uint32_t))}));
        prepared = true;
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& context,
                                                         render::graph::RenderContext& render_context,
                                                         const World& world) override {
        if (!views || !render_shader || !preprocess_shader) return {};
        const auto view =
            views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(context.view_entity());
        if (!view) return {};
        const auto& target = std::get<1>(*view);
        if (!pipeline->render_pipeline || pipeline->format != target.output_attachment.view_format) {
            const auto attributes = std::array{
                wgpu::VertexAttribute().setFormat(wgpu::VertexFormat::eFloat32x2).setOffset(0).setShaderLocation(0)};
            const auto buffers = std::array{wgpu::VertexBufferLayout()
                                                .setArrayStride(2 * sizeof(float))
                                                .setStepMode(wgpu::VertexStepMode::eVertex)
                                                .setAttributes(attributes)};
            auto layout        = world.resource<wgpu::Device>().createPipelineLayout(
                wgpu::PipelineLayoutDescriptor()
                    .setLabel("sorted-phase-geometry-layout")
                    .setBindGroupLayouts(std::array{pipeline->render_bind_group_layout}));
            pipeline->render_pipeline = world.resource<wgpu::Device>().createRenderPipeline(
                wgpu::RenderPipelineDescriptor()
                    .setLabel("sorted-phase-geometry-pipeline")
                    .setLayout(layout)
                    .setVertex(
                        wgpu::VertexState().setModule(render_shader).setEntryPoint("vertexMain").setBuffers(buffers))
                    .setFragment(wgpu::FragmentState()
                                     .setModule(render_shader)
                                     .setEntryPoint("fragmentMain")
                                     .setTargets(std::array{wgpu::ColorTargetState()
                                                                .setFormat(target.output_attachment.view_format)
                                                                .setWriteMask(wgpu::ColorWriteMask::eAll)}))
                    .setPrimitive(wgpu::PrimitiveState()
                                      .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                                      .setFrontFace(wgpu::FrontFace::eCCW)
                                      .setCullMode(wgpu::CullMode::eNone))
                    .setMultisample(wgpu::MultisampleState().setCount(1).setMask(~0u)));
            pipeline->format = target.output_attachment.view_format;
        }
        {
            auto compute = render_context.command_encoder().beginComputePass(
                wgpu::ComputePassDescriptor().setLabel("sorted-phase-geometry-preprocess-pass"));
            compute.setPipeline(pipeline->preprocess_pipeline);
            compute.setBindGroup(0, pipeline->preprocess_bind_group, std::span<const std::uint32_t>{});
            compute.dispatchWorkgroups((work_item_count + 63) / 64, 1, 1);
            compute.end();
        }
        auto pass = render_context.command_encoder().beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(
            std::array{target.out_texture_color_attachment(glm::vec4{0.10f, 0.11f, 0.14f, 1.0f})}));
        phase.render(pass, world, context.view_entity());
        pass.end();
        render_context.flush_encoder();
        return {};
    }
};

struct SortedGeometryPlugin {
    void attach(App& app) {
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            render::graph::RenderGraph graph;
            constexpr struct SortedGeometryNodeLabel {
            } kSortedGeometryNode;
            graph.add_node(render::graph::NodeLabel(kSortedGeometryNode), SortedGeometryNode{});
            render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
                render::graph::GraphLabel(kSortedGeometryGraph), std::move(graph));
        }
    }
};

}  // namespace

int main() {
    App app = App::create();
    window::Window primary_window;
    primary_window.title = "Sorted Phase Geometry Test";
    primary_window.size  = {1280, 720};
    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                          .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(SortedGeometryPlugin{});
    app.add_systems(Startup, into([](Commands commands) {
                        commands.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{},
                                       render::camera::CameraRenderGraph(kSortedGeometryGraph), transform::Transform{});
                    }));
    app.run();
}
