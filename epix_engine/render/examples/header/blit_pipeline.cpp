// Visual verification for the reusable core-pipeline BlitPipeline. This
// custom graph clears the camera intermediate texture blue, then the generic
// non-filtering BlitPipeline copies it to the real window output. The camera
// and output fallback clear are red, so a blue window proves the blit ran.

#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <optional>
#include <span>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace {
constexpr struct BlitGraphLabel {
} kBlitGraph;

void queue_blit_pipeline(Query<Item<const render::view::ViewTarget&>> views,
                         Res<core_graph::BlitPipeline> pipeline,
                         ResMut<render::SpecializedRenderPipelines<core_graph::BlitPipeline>> pipelines,
                         ResMut<render::PipelineServer> pipeline_server) {
    for (const auto& [target] : views.iter()) {
        if (!target.out_texture()) continue;
        pipelines->specialize(*pipeline_server, *pipeline,
                              {.texture_format = target.out_texture_view_format(), .samples = 1});
    }
}

struct BlitNode : render::graph::Node {
    std::optional<QueryState<Item<const render::view::ViewTarget&>>> views;

    void update(World& world) override {
        if (!views) views = world.try_query<Item<const render::view::ViewTarget&>>();
        else views->update_archetypes(world);
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& context,
                                                         render::graph::RenderContext& render_context,
                                                         const World& world) override {
        if (!views) return {};
        const auto view = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(
            context.view_entity());
        if (!view) return {};
        const auto& target = std::get<0>(*view);

        wgpu::RenderPassColorAttachment source_attachment;
        source_attachment.setView(target.main_texture_view())
            .setDepthSlice(~0u)
            .setLoadOp(wgpu::LoadOp::eClear)
            .setStoreOp(wgpu::StoreOp::eStore)
            .setClearValue(wgpu::Color(0.04, 0.42, 0.92, 1.0));
        auto source_pass = render_context.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(std::array{source_attachment}));
        source_pass.end();

        const auto& pipeline = world.resource<core_graph::BlitPipeline>();
        const core_graph::BlitPipelineKey key{.texture_format = target.out_texture_view_format(), .samples = 1};
        const auto& pipelines = world.resource<render::SpecializedRenderPipelines<core_graph::BlitPipeline>>();
        const auto cached = pipelines.cache.find(key);
        if (cached == pipelines.cache.end()) return {};
        const auto ready = world.resource<render::PipelineServer>().get_render_pipeline(cached->second);
        if (!ready) return {};

        const auto bind_group = pipeline.create_bind_group(render_context.device(), target.main_texture_view());
        auto output_pass = render_context.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(
                std::array{target.out_texture_color_attachment(glm::vec4(0.90f, 0.04f, 0.08f, 1.0f))}));
        output_pass.setPipeline(ready->get().pipeline());
        output_pass.setBindGroup(0, bind_group, std::span<const std::uint32_t>{});
        output_pass.draw(3, 1, 0, 0);
        output_pass.end();
        render_context.flush_encoder();
        return {};
    }
};

struct BlitGraphPlugin {
    void attach(App& app) {
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            render::graph::RenderGraph graph;
            constexpr struct BlitNodeLabel {
            } kBlitNode;
            graph.add_node(render::graph::NodeLabel(kBlitNode), BlitNode{});
            render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
                render::graph::GraphLabel(kBlitGraph), std::move(graph));
            render_app->get().add_systems(
                render::Render,
                into(queue_blit_pipeline).in_set(render::RenderSystems::Queue).set_name("queue blit pipeline"));
        }
    }
};
}  // namespace

int main() {
    App app = App::create();
    window::Window primary_window;
    primary_window.title = "Blit Pipeline";
    primary_window.size  = {960, 540};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                          .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(BlitGraphPlugin{});

    app.add_systems(Startup, into([](Commands commands) {
                        camera::Camera camera;
                        camera.clear_color = camera::ClearColorConfig::Custom{
                            camera::ClearColor{0.90f, 0.04f, 0.08f, 1.0f}};
                        commands.spawn(std::move(camera), camera::Projection{},
                                       render::camera::CameraRenderGraph(kBlitGraph), render::view::Msaa::Off,
                                       transform::Transform{});
                    }));
    app.run();
}
