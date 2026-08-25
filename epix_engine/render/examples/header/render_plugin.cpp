// Minimal render-module-only example: opens a window, registers a custom
// render sub-graph whose single node clears the swapchain output, and
// spawns a camera wired to it. A window filled with the static clear
// color confirms the render loop (extract -> camera driver -> graph
// node -> present) works end to end.

#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/camera.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
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

        const auto clear_color = [&]() -> std::optional<glm::vec4> {
            if (std::holds_alternative<::epix::camera::ClearColorConfig::None>(camera.clear_color)) return std::nullopt;
            if (const auto* custom = std::get_if<::epix::camera::ClearColorConfig::Custom>(&camera.clear_color)) {
                return custom->color.to_vec4();
            }
            return world.get_resource<::epix::camera::ClearColor>()
                .transform([](const auto& color) { return color.get().to_vec4(); });
        }();
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

}  // namespace

// Demonstrates Bevy RenderCreation::Manual semantics: the embedding program
// owns adapter/device creation and supplies the direct wgpu resources to
// RenderPlugin.  The graph below is intentionally independent of any shader
// pipeline so that this is also a small, runnable host-integration example.
render::RenderResources create_manual_render_resources() {
    wgpu::Instance instance = wgpu::createInstance();
    wgpu::Adapter adapter   = instance.requestAdapter(
        wgpu::RequestAdapterOptions().setPowerPreference(wgpu::PowerPreference::eHighPerformance));
    if (!adapter) throw std::runtime_error("Unable to create adapter for manual render creation example");

    wgpu::Device device = adapter.requestDevice(
        wgpu::DeviceDescriptor().setLabel("Manual Render Creation Device").setDefaultQueue(wgpu::QueueDescriptor{}));
    if (!device) throw std::runtime_error("Unable to create device for manual render creation example");
    return render::RenderResources{
        .device       = device,
        .queue        = device.getQueue(),
        .adapter_info = render::RenderAdapterInfo::from_adapter(adapter),
        .adapter      = adapter,
        .instance     = instance,
    };
}

int main() {
    App app = App::create();

    render::RenderPlugin render_plugin;
    render_plugin.render_creation = render::RenderCreation::manual(create_manual_render_resources());

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
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(std::move(render_plugin))
        .add_plugins(ClearGraphPlugin{});

    // A camera wired to our custom render graph (a registered sub-graph;
    // an unregistered label makes the camera driver fail and nothing presents).
    app.add_systems(Startup, into([](Commands cmd) {
                        cmd.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{}, render::camera::CameraRenderGraph(kClearGraph),
                                  transform::Transform{});
                    }));

    app.run();
}
// rebuild
