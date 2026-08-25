#include <imgui.h>
#include <spdlog/spdlog.h>

#include <epix/core_graph.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/render/imgui.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

void demo_system(imgui::Ctx imgui) { ImGui::ShowDemoWindow(); }

void hello_system(imgui::Ctx imgui) {
    ImGui::Begin("Hello from Engine");
    ImGui::Text("ImGui is working in a multithreaded ECS!");
    ImGui::Text("This system runs on a worker thread.");
    ImGui::Text("No manual SetCurrentContext needed.");
    if (ImGui::Button("Click me")) {
        spdlog::info("Button clicked!");
    }
    ImGui::End();
}

int main() {
    App app = App::create();

    epix::window::Window primary_window;
    primary_window.title = "ImGui Integration Test";
    primary_window.size  = {1280, 720};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(epix::window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = epix::window::ExitCondition::OnPrimaryClosed,
        })
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
        .add_plugins(core_graph::core_2d::Core2dPlugin{})
        .add_plugins(imgui::ImGuiPlugin{
            .enable_docking   = true,
            .enable_viewports = true,
        });

    app.add_systems(Startup,
                    into([](Commands cmd) { cmd.spawn(camera::Camera2d{}, transform::Transform{}); }));

    app.add_systems(Update, into(demo_system, hello_system).set_names(std::array{"imgui demo", "imgui hello"}));

    app.run();
}
