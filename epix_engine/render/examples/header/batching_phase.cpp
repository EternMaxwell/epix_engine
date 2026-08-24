// Visual verification for the generic automatic binned-phase batching API.
//
// The render graph runs the CPU and GPU-preprocessing `GetFullBatchData`
// preparation entry points that a renderer uses, then displays green only
// when both produce the expected contiguous batches/work-item metadata. This
// intentionally validates the reusable phase APIs without relying on the mesh
// subsystem's separate batching implementation.

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
#include <optional>
#include <utility>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace batching_phase_demo {

struct BatchSetKey {
    int value = 0;
    bool indexed_value = true;

    BatchSetKey() = default;
    BatchSetKey(int value, bool indexed = true) : value(value), indexed_value(indexed) {}
    bool indexed() const noexcept { return indexed_value; }
    auto operator<=>(const BatchSetKey&) const = default;
};

struct Item {
    Entity render_entity{};
    render::phase::DrawFunctionId draw_id{};
    int bin = 0;
    BatchSetKey batch_set{};
    std::pair<std::uint32_t, std::uint32_t> batch_range{0, 1};

    Entity entity() const noexcept { return render_entity; }
    render::sync_world::MainEntity main_entity() const noexcept { return {render_entity}; }
    int sort_key() const noexcept { return 0; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return {}; }
    using BinKey = int;
    using BatchSetKey = batching_phase_demo::BatchSetKey;
    const int& bin_key() const noexcept { return bin; }
    const BatchSetKey& batch_set_key() const noexcept { return batch_set; }
    bool batchable() const noexcept { return true; }
};

struct Adapter {};

}  // namespace batching_phase_demo

template <>
struct std::hash<batching_phase_demo::BatchSetKey> {
    std::size_t operator()(const batching_phase_demo::BatchSetKey& key) const noexcept {
        return std::hash<int>{}(key.value) ^ (std::hash<bool>{}(key.indexed_value) << 1);
    }
};

template <>
struct render::batching::GetBatchData<batching_phase_demo::Adapter> {
    using Param = World&;
    using CompareData = std::uint32_t;
    using BufferData = std::uint32_t;

    std::optional<std::pair<BufferData, std::optional<CompareData>>> get_batch_data(
        World&, std::pair<Entity, render::sync_world::MainEntity>) const {
        return std::nullopt;
    }
};

template <>
struct render::batching::GetFullBatchData<batching_phase_demo::Adapter> {
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

static_assert(render::phase::BinnedPhaseItem<batching_phase_demo::Item>);
static_assert(render::batching::GetFullBatchDataImpl<batching_phase_demo::Adapter>);

constexpr struct BatchingGraphLabel {
} kBatchingGraph;

bool prepare_demo_batches() {
    using Item = batching_phase_demo::Item;
    render::phase::BinnedRenderPhase<Item> phase{render::batching::GpuPreprocessingMode::None};
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
    World scratch_world{WorldId{501}};
    render::batching::batch_and_prepare_binned_phase<Item, batching_phase_demo::Adapter>(phase, instances,
                                                                                            scratch_world);

    const auto* first = phase.batchable_meshes.get({batching_phase_demo::BatchSetKey{0}, 0});
    const auto* second = phase.batchable_meshes.get({batching_phase_demo::BatchSetKey{0}, 1});
    const bool cpu_ready = first && second && first->batches.size() == 1 && second->batches.size() == 1 &&
                           first->batches.front().instance_range == std::pair<std::uint32_t, std::uint32_t>{0, 3} &&
                           second->batches.front().instance_range == std::pair<std::uint32_t, std::uint32_t>{3, 5};

    render::phase::BinnedRenderPhase<Item> gpu_phase{render::batching::GpuPreprocessingMode::Culling};
    gpu_phase.add(0, 0, Entity::from_index(300), render::sync_world::MainEntity{Entity::from_index(6)},
                  render::phase::InputUniformIndex{6}, render::phase::BinnedRenderPhaseType::MultidrawableMesh, tick);
    gpu_phase.add(0, 1, Entity::from_index(301), render::sync_world::MainEntity{Entity::from_index(7)},
                  render::phase::InputUniformIndex{7}, render::phase::BinnedRenderPhaseType::MultidrawableMesh, tick);
    render::batching::UntypedPhaseBatchedInstanceBuffers<std::uint32_t> phase_buffers;
    render::batching::UntypedPhaseIndirectParametersBuffers indirect_parameters;
    const render::view::RetainedViewEntity retained_view{render::sync_world::MainEntity{Entity::from_index(50)},
                                                          std::nullopt, 0};
    render::batching::batch_and_prepare_gpu_binned_phase<Item, batching_phase_demo::Adapter>(
        gpu_phase, phase_buffers, indirect_parameters, retained_view, false, false, scratch_world);
    const auto* work = phase_buffers.work_item_buffers.contains(retained_view)
                           ? &phase_buffers.work_item_buffers.at(retained_view)
                           : nullptr;
    const bool gpu_ready = work && phase_buffers.data_buffer.len() == 2 && indirect_parameters.indexed_data.len() == 2 &&
                           indirect_parameters.indexed_batch_sets.len() == 1 &&
                           std::get<render::batching::PreprocessWorkItemBuffers::Indirect>(work->storage).indexed.len() == 2 &&
                           std::get<2>(gpu_phase.batch_sets).size() == 1;
    return cpu_ready && gpu_ready;
}

struct BatchingPhaseNode : render::graph::Node {
    std::optional<QueryState<Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>> views;
    bool prepared = false;
    bool valid = false;

    void update(World& world) override {
        if (!views) {
            views = world.try_query<Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>();
        } else {
            views->update_archetypes(world);
        }
        if (!prepared) {
            valid = prepare_demo_batches();
            prepared = true;
        }
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& context,
                                                          render::graph::RenderContext& render_context,
                                                          const World& world) override {
        if (!views) return {};
        const auto view = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(context.view_entity());
        if (!view) return {};
        const auto& target = std::get<1>(*view);
        const glm::vec4 color = valid ? glm::vec4{0.05f, 0.50f, 0.16f, 1.0f}
                                      : glm::vec4{0.65f, 0.04f, 0.04f, 1.0f};
        auto pass = render_context.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(std::array{target.out_texture_color_attachment(color)}));
        pass.end();
        render_context.flush_encoder();
        return {};
    }
};

struct BatchingPhasePlugin {
    void attach(App& app) {
        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) return;
        render::graph::RenderGraph graph;
        constexpr struct BatchingNodeLabel {
        } kBatchingNode;
        graph.add_node(render::graph::NodeLabel(kBatchingNode), BatchingPhaseNode{});
        render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
            render::graph::GraphLabel(kBatchingGraph), std::move(graph));
    }
};

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "Automatic Batch Preparation Visual Test (green = prepared)";
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
        .add_plugins(BatchingPhasePlugin{});

    app.add_systems(Startup, into([](Commands commands) {
                        commands.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{},
                                       render::camera::CameraRenderGraph(kBatchingGraph), transform::Transform{});
                    }));
    app.run();
}
