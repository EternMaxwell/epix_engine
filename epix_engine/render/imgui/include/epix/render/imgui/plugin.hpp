#pragma once

#include <epix/core.hpp>
#include <epix/glfw/core.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/render/imgui/state.hpp>
#include <epix/window.hpp>
#include <webgpu/webgpu.hpp>
namespace epix::imgui {
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
struct ImGuiPlugin {
    bool enable_docking   = false;
    bool enable_viewports = false;

    ImGuiPlugin& set_docking(bool enabled = true) noexcept;
    ImGuiPlugin& set_viewports(bool enabled = true) noexcept;

    void attach(epix::core::App& app);
    void detach(epix::core::App& app);
};

inline struct BeginFrameSetT {
} BeginFrameSet;

// Frame lifecycle systems (main world)
void imgui_begin_frame(
    epix::core::ResMut<ImGuiState> state,
    epix::core::Res<epix::glfw::GLFWwindows> windows,
    epix::core::Query<epix::core::Item<epix::core::Entity>,
                      epix::core::With<::epix::window::Window, ::epix::window::PrimaryWindow>> primary);
void imgui_end_frame(epix::core::ResMut<ImGuiState> state);

// Post-PreUpdate system that consumes input events handled by ImGui.
// Checks ImGui::GetIO().WantCapture* flags and advances the event head
// so later schedules do not see consumed events.
void imgui_consume_input(epix::core::Res<ImGuiState> state,
                         epix::core::ResMut<epix::core::Events<input::KeyInput>> key_events,
                         epix::core::ResMut<epix::core::Events<input::MouseButtonInput>> mouse_events,
                         epix::core::ResMut<epix::core::Events<input::MouseScroll>> scroll_events,
                         epix::core::ResMut<input::ButtonInput<input::KeyCode>> key_input,
                         epix::core::ResMut<input::ButtonInput<input::MouseButton>> mouse_input);

// Render system (render sub-app)
void imgui_render(epix::core::Res<ImGuiState> state,
                  epix::core::Res<epix::render::window::ExtractedWindows> windows,
                  epix::core::Res<wgpu::Instance> instance,
                  epix::core::Res<wgpu::Adapter> adapter,
                  epix::core::Res<wgpu::Device> device,
                  epix::core::Res<wgpu::Queue> queue);
}  // namespace epix::imgui
