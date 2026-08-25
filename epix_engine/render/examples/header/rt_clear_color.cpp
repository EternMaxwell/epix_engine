// Minimal render-module-only real-time example: opens a window and animates
// the camera's clear color each frame. The camera drives a custom sub-graph
// whose single node clears the swapchain output attachment directly, so the
// full render loop (extract -> camera driver -> graph node -> present) is
// exercised without any module that depends on the render module.
//
// A window showing a smoothly cycling background plus a per-second FPS line
// means the swapchain is being presented every frame.

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <optional>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

namespace {

// Label for our custom sub-graph (an empty type gives a unique label).
constexpr struct ClearGraphLabel {
} kClearGraph;

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

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& ctx,
                                                          render::graph::RenderContext& render_ctx,
                                                          const World& world) override {
        if (!views) return {};
        auto view_entity = ctx.view_entity();
        auto view_opt = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view_entity);
        if (!view_opt) return {};
        auto&& [camera, target] = *view_opt;

        // Clear the swapchain output directly (no post-process / blit needed
        // for a clear-only pass; writing out_texture marks it for present).
        const auto clear_color = camera.clear_color.type == ::epix::camera::ClearColorConfig::Type::None
                                     ? std::optional<glm::vec4>{}
                                     : camera.clear_color.type == ::epix::camera::ClearColorConfig::Type::Custom
                                           ? std::optional<glm::vec4>{camera.clear_color.clear_color.to_vec4()}
                                           : world.get_resource<::epix::camera::ClearColor>()
                                                 .transform([](const auto& color) { return color.get().to_vec4(); });
        auto pass = render_ctx.command_encoder().beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(
            std::array{target.out_texture_color_attachment(clear_color)}));
        pass.end();
        render_ctx.flush_encoder();
        return {};
    }
};

// Registers the custom sub-graph in the render app's RenderGraph.
struct ClearGraphPlugin {
    void attach(app::App& app) {
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            render::graph::RenderGraph graph;
            constexpr struct ClearPassNodeLabel {
            } kClearPass;
            graph.add_node(render::graph::NodeLabel(kClearPass), ClearPassNode{});
            render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
                render::graph::GraphLabel(kClearGraph), std::move(graph));
        }
    }
};

// Animates the render-world ClearColor (the one extract_cameras reads) from
// the extracted render-world Time. Runs in the Render schedule each frame.
void animate_clear_color(ResMut<::epix::camera::ClearColor> color, Res<time::Time<>> time) {
    const float t = time->elapsed_secs();
    auto& c       = *color;
    c.r           = 0.5f + 0.5f * std::sin(t * 0.8f);
    c.g           = 0.5f + 0.5f * std::sin(t * 1.3f + 2.0f);
    c.b           = 0.5f + 0.5f * std::sin(t * 1.9f + 4.0f);
}

// Logs one FPS line per second from the main world.
void log_fps(Res<time::Time<>> time) {
    static float last_log = 0.0f;
    const float now       = time->elapsed_secs();
    if (now - last_log >= 1.0f) {
        spdlog::info("[rt] {:.1f}s elapsed, {:.1f} fps", now, 1.0f / std::max(time->delta_secs(), 0.0001f));
        last_log = now;
    }
}

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "RT Clear Color";
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
        .add_plugins(render::RenderPlugin{})
        .add_plugins(ClearGraphPlugin{});

    // A camera wired to our custom render graph (a registered sub-graph;
    // an unregistered label makes the camera driver fail and nothing presents).
    app.add_systems(Startup, into([](Commands cmd) {
                        cmd.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{}, render::camera::CameraRenderGraph(kClearGraph),
                                  transform::Transform{});
                    }));

    // Animate the render-world ClearColor; log FPS from the main world.
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().add_systems(render::Render, into(animate_clear_color).set_name("animate clear color"));
    }
    app.add_systems(Update, into(log_fps).set_name("log fps"));

    app.run();
}
// rebuild
