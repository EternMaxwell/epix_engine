#include <imgui.h>
#include <spdlog/spdlog.h>

import epix.core;
import epix.input;
import epix.window;
import epix.transform;
import epix.render;
import epix.core_graph;
import epix.glfw.core;
import epix.glfw.render;
import epix.render.imgui;

import std;

using namespace core;

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

    window::Window primary_window;
    primary_window.title = "ImGui Integration Test";
    primary_window.size  = {1280, 720};

    app.add_plugins(window::WindowPlugin{
                        .primary_window = primary_window,
                        .exit_condition = window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::core_2d::Core2dPlugin{})
        .add_plugins(imgui::ImGuiPlugin{});

    app.add_systems(Startup, into([](Commands cmd) { cmd.spawn(core_graph::core_2d::Camera2DBundle{}); }));

    app.add_systems(Update, into(demo_system, hello_system).set_names(std::array{"imgui demo", "imgui hello"}));

    app.run();
}
