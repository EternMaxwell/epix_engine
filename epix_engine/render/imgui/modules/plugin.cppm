module;

export module epix.render.imgui:plugin;

import :state;
import epix.core;
import epix.render;
import epix.window;
import epix.glfw.core;
import webgpu;
import std;

using namespace core;

namespace imgui {
/** @brief Plugin that integrates Dear ImGui with the engine.
 *
 *  Sets up the ImGui context, GLFW and WebGPU backends, frame lifecycle
 *  systems, and a render system that draws ImGui output on top of the
 *  scene each frame.
 *
 *  Usage:
 *      app.add_plugins(imgui::ImGuiPlugin{});
 *      app.add_systems(Update, into(my_system));
 *
 *  where:
 *      void my_system(imgui::Ctx imgui) {
 *          ImGui::Begin("Hello");
 *          ImGui::Text("World");
 *          ImGui::End();
 *      }
 */
export struct ImGuiPlugin {
    void build(App& app);
};

// Frame lifecycle systems (main world)
void imgui_begin_frame(ResMut<ImGuiState> state,
                       Res<glfw::GLFWwindows> windows,
                       Query<Item<Entity>, With<::window::Window, ::window::PrimaryWindow>> primary);
void imgui_end_frame(ResMut<ImGuiState> state);

// Render system (render sub-app)
void imgui_render(Res<ImGuiState> state,
                  Res<render::window::ExtractedWindows> windows,
                  Res<wgpu::Device> device,
                  Res<wgpu::Queue> queue);
}  // namespace imgui
