// A visual test for the standard automatic CPU binned-phase API.
//
// This intentionally does not encode a pass/fail color. The graph clears the
// output to a neutral background, then `BinnedRenderPhase::render` invokes a
// real draw function. The two prepared batches issue instanced
// triangle draws, so a bad preparation/render path leaves geometry missing.

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
    wgpu::Buffer vertex_buffer;
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
        pass.setVertexBuffer(0, state->vertex_buffer, 0, 3 * sizeof(float) * 2);
        pass.draw(3, item.batch_range.second - item.batch_range.first, 0, item.batch_range.first);
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
                                                  std::uint32_t,
                                                  std::optional<std::uint32_t>,
                                                  render::batching::UntypedPhaseIndirectParametersBuffers&,
                                                  std::uint32_t) const {}
};

namespace {

using binned_phase_geometry::Adapter;
using binned_phase_geometry::Item;

static_assert(render::phase::BinnedPhaseItem<Item>);
static_assert(render::batching::GetFullBatchDataImpl<Adapter>);

// The engine's pipeline assets use Slang. This deliberately tiny direct-wgpu
// shader is test scaffolding for the accepted direct-resource layer: it keeps
// the visual proof focused on the generic binned-phase draw ranges.
constexpr std::string_view kTriangleShader = R"(
struct VertexInput {
    @location(0) position: vec2<f32>,
    @builtin(instance_index) instance_index: u32,
};

@vertex
fn vertexMain(input: VertexInput) -> @builtin(position) vec4<f32> {
    let column = input.instance_index % 3u;
    let row = input.instance_index / 3u;
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
        for (std::uint32_t index = 0; index < 3; ++index) {
            phase.add(0, 0, Entity::from_index(100 + index), render::sync_world::MainEntity{Entity::from_index(index + 1)},
                      render::phase::InputUniformIndex{index}, render::phase::BinnedRenderPhaseType::BatchableMesh, tick);
        }
        for (std::uint32_t index = 0; index < 2; ++index) {
            phase.add(0, 1, Entity::from_index(200 + index), render::sync_world::MainEntity{Entity::from_index(index + 4)},
                      render::phase::InputUniformIndex{index + 3}, render::phase::BinnedRenderPhaseType::BatchableMesh, tick);
        }
        wgpu::Limits limits{};
        limits.maxStorageBuffersPerShaderStage = 1;
        render::render_resource::GpuArrayBuffer<std::uint32_t> instances{limits};
        render::batching::batch_and_prepare_binned_phase<Item, Adapter>(phase, instances, world);

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
        if (!shader_module) throw std::runtime_error("failed to create binned-phase test shader module");
        prepared = true;
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& context,
                                                          render::graph::RenderContext& render_context,
                                                          const World& world) override {
        if (!views || !shader_module) return {};
        const auto view = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(context.view_entity());
        if (!view) return {};
        const auto& target = std::get<1>(*view);
        if (!pipeline->pipeline || pipeline->format != target.output_attachment.view_format) {
            const auto format = target.output_attachment.view_format;
            // The Slang modules deliberately have no bindings. Supplying the
            // empty layout explicitly avoids wgpu's SPIR-V interface
            // reflection path, which is unrelated to the phase under test.
            auto layout = world.resource<wgpu::Device>().createPipelineLayout(
                wgpu::PipelineLayoutDescriptor().setLabel("binned-phase-geometry-layout"));
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
