#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <epix/app.hpp>
#include <epix/glfw/core.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/window.hpp>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/imgui/state.hpp>

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
EPIX_EXPORT struct ImGuiPlugin {
    bool enable_docking   = false;
    bool enable_viewports = false;

    ImGuiPlugin& set_docking(bool enabled = true) noexcept;
    ImGuiPlugin& set_viewports(bool enabled = true) noexcept;

    void attach(app::App& app);
    void detach(app::App& app);
};

EPIX_EXPORT inline struct BeginFrameSetT {
} BeginFrameSet;

// Frame lifecycle systems (main world)
void imgui_begin_frame(
    ecs::ResMut<ImGuiState> state,
    ecs::Res<glfw::GLFWwindows> windows,
    ecs::Query<ecs::Item<ecs::Entity>, ecs::With<::epix::window::Window, ::epix::window::PrimaryWindow>> primary);
void imgui_end_frame(ecs::ResMut<ImGuiState> state);

// Post-PreUpdate system that consumes input events handled by ImGui.
// Checks ImGui::GetIO().WantCapture* flags and advances the event head
// so later schedules do not see consumed events.
void imgui_consume_input(ecs::Res<ImGuiState> state,
                         ecs::ResMut<ecs::Events<input::KeyInput>> key_events,
                         ecs::ResMut<ecs::Events<input::MouseButtonInput>> mouse_events,
                         ecs::ResMut<ecs::Events<input::MouseScroll>> scroll_events,
                         ecs::ResMut<input::ButtonInput<input::KeyCode>> key_input,
                         ecs::ResMut<input::ButtonInput<input::MouseButton>> mouse_input);

// Render system (render sub-app)
void imgui_render(ecs::Res<ImGuiState> state,
                  ecs::Res<render::window::ExtractedWindows> windows,
                  ecs::Res<wgpu::Instance> instance,
                  ecs::Res<wgpu::Adapter> adapter,
                  ecs::Res<wgpu::Device> device,
                  ecs::Res<wgpu::Queue> queue);
}  // namespace epix::imgui
