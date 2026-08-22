// Minimal render-module-only example: opens a window, registers a custom
// render sub-graph whose single node clears the swapchain output, and
// spawns a camera wired to it. A window filled with the static clear
// color confirms the render loop (extract -> camera driver -> graph
// node -> present) works end to end.

#include <spdlog/spdlog.h>

#include <optional>

#include <spdlog/spdlog.h>

#include <stacktrace>

import epix.ecs;
import epix.input;
import epix.window;
import epix.transform;
import epix.render;
import epix.glfw.core;
import epix.glfw.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

namespace {

// Label for our custom sub-graph (an empty type gives a unique label).
constexpr struct ClearGraphLabel {} kClearGraph;

// The single node of the custom graph: clears the view's swapchain output
// attachment with the camera's clear color each frame.
struct ClearPassNode : render::graph::Node {
    std::optional<QueryState<Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>> views;

    void update(World& world) override {
        if (!views) {
            views = world.try_query<Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>();
        } else {
            views->update_archetypes(world);
        }
    }

    void run(render::graph::GraphContext& ctx, render::graph::RenderContext& render_ctx,
             const World& world) override {
        if (!views) return;
        auto view_entity = ctx.view_entity();
        auto view_opt    = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view_entity);
        if (!view_opt) return;
        auto&& [camera, target] = *view_opt;

        std::optional<glm::vec4> clear_color;
        if (camera.clear_color) clear_color = *camera.clear_color;
        auto pass = render_ctx.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(
                std::array{target.out_texture.get_attachment(clear_color)}));
        pass.end();
        render_ctx.flush_encoder();
    }
};

// Registers the custom sub-graph in the render app's RenderGraph.
struct ClearGraphPlugin {
    void attach(app::App& app) {
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            render::graph::RenderGraph graph;
            constexpr struct ClearPassNodeLabel {} kClearPass;
            graph.add_node(render::graph::NodeLabel(kClearPass), ClearPassNode{});
            render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
                render::graph::GraphLabel(kClearGraph), std::move(graph));
        }
    }
};

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "Render Plugin";
    primary_window.size  = {1280, 720};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(ClearGraphPlugin{});

    // A camera wired to our custom render graph (a registered sub-graph;
    // an unregistered label makes the camera driver fail and nothing presents).
    app.add_systems(Startup,
                    into([](Commands cmd) { cmd.spawn(camera::Camera{}, render::camera::CameraRenderGraph(kClearGraph), transform::Transform{}); }));

    app.run();
}