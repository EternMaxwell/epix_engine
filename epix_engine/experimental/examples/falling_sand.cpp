#include <imgui.h>
import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.render.imgui;
import epix.core_graph;
import epix.mesh;
import epix.transform;
import epix.input;
import epix.extension.grid;
import epix.experimental.fallingsand;

using namespace core;

void settings_ui(imgui::Ctx imgui_ctx, ResMut<ext::fallingsand::SandSimulation> sim) {
    ImGui::Begin("Falling Sand Settings");

    bool paused = sim->paused();
    if (ImGui::Checkbox("Paused (Space)", &paused)) sim->set_paused(paused);

    bool auto_fall = sim->auto_fall();
    if (ImGui::Checkbox("Auto Fall (T)", &auto_fall)) sim->set_auto_fall(auto_fall);

    int brush = sim->brush_radius();
    if (ImGui::SliderInt("Brush Radius (Q/E)", &brush, 1, 32)) sim->set_brush_radius(brush);

    float cell = sim->cell_size();
    if (ImGui::SliderFloat("Cell Size", &cell, 1.0f, 16.0f, "%.1f")) sim->set_cell_size(cell);

    ImGui::Separator();
    ImGui::Text("Tick: %llu", static_cast<unsigned long long>(sim->tick()));

    if (ImGui::Button("Reset (R)")) sim->reset();
    ImGui::SameLine();
    if (ImGui::Button("Clear (C)")) sim->clear_all();

    ImGui::End();
}

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Falling Sand Interactive Test (LMB add, RMB erase, Space pause, R reset)";
    primary_window.size  = {1280, 720};

    app.add_plugins(window::WindowPlugin{
                        .primary_window = primary_window,
                        .exit_condition = window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(imgui::ImGuiPlugin{})
        .add_plugins(ext::fallingsand::SimpleFallingSandPlugin{})
        .add_systems(PreUpdate, into(settings_ui));

    app.run();
}
