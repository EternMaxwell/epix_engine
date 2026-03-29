module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>
#include <spdlog/spdlog.h>

module epix.render.imgui;

import epix.core;
import epix.input;
import epix.render;
import epix.window;
import epix.glfw.core;
import webgpu;
import std;

using namespace epix;
using namespace epix::core;

namespace win  = ::epix::window;
namespace rwin = epix::render::window;

void imgui::ImGuiPlugin::build(App& app) {
    // Create ImGui context and store in main world
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Tell ImGui that the renderer will handle texture creation on-demand.
    // This must be set BEFORE any NewFrame() call so that the font atlas
    // build takes the new (non-legacy) path and avoids preloading all glyphs.
    // The WebGPU backend also sets this flag during Init(), which is a no-op
    // since the flag is already present.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    app.world_mut().insert_resource(ImGuiState{
        .ctx         = ctx,
        .initialized = false,
    });

    app.configure_sets(PreUpdate, sets(BeginFrameSet));

    // Main world frame systems
    app.add_systems(PreUpdate, into(imgui_begin_frame).set_name("imgui begin frame").in_set(BeginFrameSet));
    app.add_systems(Last, into(imgui_end_frame).set_name("imgui end frame"));

    // Consume input events that ImGui handled after every schedule.
    // Resets WantCapture flags so events are only consumed once per frame.
    app.add_post_systems(into(imgui_consume_input).set_name("imgui consume input"));

    // Extract ImGuiState to render world
    app.add_plugins(render::ExtractResourcePlugin<ImGuiState>{});

    // Render sub-app: add ImGui render system after the render graph
    app.sub_app_mut(render::Render).then([](App& render_app) {
        render_app.add_systems(render::Render, into(imgui_render)
                                                   .set_name("imgui render")
                                                   .after(render::RenderSet::Render)
                                                   .before(render::RenderSet::Cleanup));
    });
}

void imgui::imgui_begin_frame(ResMut<ImGuiState> state,
                              Res<glfw::GLFWwindows> windows,
                              Query<Item<Entity>, With<win::Window, win::PrimaryWindow>> primary) {
    if (!state->ctx) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(state->ctx));

    // Lazy-initialize backends on first frame when the primary window is available
    if (!state->initialized) {
        std::optional<Entity> primary_entity;
        for (auto&& [entity] : primary.iter()) {
            primary_entity = entity;
            break;
        }
        if (!primary_entity) return;  // no primary window yet

        auto it = windows->find(*primary_entity);
        if (it == windows->end()) return;  // GLFW window not created yet
        GLFWwindow* glfw_window = it->second;

        // Init GLFW backend (chain callbacks so engine events still work)
        ImGui_ImplGlfw_InitForOther(glfw_window, true);

        state->initialized = true;
        spdlog::info("[imgui] Initialized GLFW backend.");
    }

    if (state->initialized) {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        state->frame_active = true;
    }
}

void imgui::imgui_end_frame(ResMut<ImGuiState> state) {
    if (!state->ctx || !state->frame_active) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(state->ctx));
    ImGui::EndFrame();
    ImGui::Render();

    // Snapshot the draw data so the render sub-app can use it safely even
    // after the main world starts the next frame (pipelined rendering).
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd && dd->CmdLists.Size > 0) {
        auto snap             = std::make_shared<DrawDataSnapshot>();
        snap->valid           = true;
        snap->display_pos_x   = dd->DisplayPos.x;
        snap->display_pos_y   = dd->DisplayPos.y;
        snap->display_size_x  = dd->DisplaySize.x;
        snap->display_size_y  = dd->DisplaySize.y;
        snap->fb_scale_x      = dd->FramebufferScale.x;
        snap->fb_scale_y      = dd->FramebufferScale.y;
        snap->total_vtx_count = dd->TotalVtxCount;
        snap->total_idx_count = dd->TotalIdxCount;
        snap->draw_lists.resize(dd->CmdLists.Size);
        for (int i = 0; i < dd->CmdLists.Size; i++) {
            const ImDrawList* src = dd->CmdLists[i];
            auto& dst             = snap->draw_lists[i];
            dst.cmd_count         = src->CmdBuffer.Size;
            dst.idx_count         = src->IdxBuffer.Size;
            dst.vtx_count         = src->VtxBuffer.Size;
            dst.cmd_buffer.resize(src->CmdBuffer.Size * sizeof(ImDrawCmd));
            dst.idx_buffer.resize(src->IdxBuffer.Size * sizeof(ImDrawIdx));
            dst.vtx_buffer.resize(src->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(dst.cmd_buffer.data(), src->CmdBuffer.Data, dst.cmd_buffer.size());
            memcpy(dst.idx_buffer.data(), src->IdxBuffer.Data, dst.idx_buffer.size());
            memcpy(dst.vtx_buffer.data(), src->VtxBuffer.Data, dst.vtx_buffer.size());
        }
        state->draw_snapshot = std::move(snap);
    } else {
        state->draw_snapshot.reset();
    }

    state->frame_active = false;
}

void imgui::imgui_consume_input(Res<ImGuiState> state,
                                ResMut<Events<input::KeyInput>> key_events,
                                ResMut<Events<input::MouseButtonInput>> mouse_events,
                                ResMut<Events<input::MouseScroll>> scroll_events,
                                ResMut<input::ButtonInput<input::KeyCode>> key_input,
                                ResMut<input::ButtonInput<input::MouseButton>> mouse_input) {
    if (!state->ctx || !state->initialized) return;
    state->activate();
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) {
        key_events->advance_head(key_events->tail());
        key_input->bypass_pressed();
        key_input->bypass_just_pressed();
        key_input->bypass_just_released();
        io.WantCaptureKeyboard = false;
    }
    if (io.WantCaptureMouse) {
        mouse_events->advance_head(mouse_events->tail());
        scroll_events->advance_head(scroll_events->tail());
        mouse_input->bypass_pressed();
        mouse_input->bypass_just_pressed();
        mouse_input->bypass_just_released();
        io.WantCaptureMouse = false;
    }
}

void imgui::imgui_render(Res<ImGuiState> state,
                         Res<render::window::ExtractedWindows> windows,
                         Res<wgpu::Device> device,
                         Res<wgpu::Queue> queue) {
    if (!state->ctx) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(state->ctx));

    auto snap = state->draw_snapshot;
    if (!snap || !snap->valid || snap->draw_lists.empty()) return;

    // Find the first window with a valid swapchain texture view
    const render::window::ExtractedWindow* target_window = nullptr;
    for (const auto& [entity, window] : windows->windows) {
        if (window.swapchain_texture_view) {
            target_window = &window;
            break;
        }
    }
    if (!target_window) return;
    const auto& window = *target_window;

    // Lazy-initialize WebGPU backend
    static bool wgpu_initialized = false;
    if (!wgpu_initialized) {
        ImGui_ImplWGPU_InitInfo init_info{};
        init_info.Device             = *device;
        init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(window.swapchain_texture_format);
        init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
        init_info.NumFramesInFlight  = 3;
        ImGui_ImplWGPU_Init(&init_info);
        wgpu_initialized = true;
        spdlog::info("[imgui] Initialized WebGPU backend (format={}).",
                     static_cast<int>(window.swapchain_texture_format));
    }
    ImGui_ImplWGPU_NewFrame();

    // Reconstruct temporary ImDrawList/ImDrawData from the snapshot so
    // RenderDrawData works with self-contained, race-free data.
    std::vector<ImDrawList> draw_lists_storage(snap->draw_lists.size(), ImDrawList(nullptr));
    std::vector<ImDrawList*> draw_list_ptrs(snap->draw_lists.size());
    for (size_t i = 0; i < snap->draw_lists.size(); i++) {
        const auto& src = snap->draw_lists[i];
        ImDrawList& dl  = draw_lists_storage[i];
        dl.CmdBuffer.resize(src.cmd_count);
        dl.IdxBuffer.resize(src.idx_count);
        dl.VtxBuffer.resize(src.vtx_count);
        memcpy(dl.CmdBuffer.Data, src.cmd_buffer.data(), src.cmd_buffer.size());
        memcpy(dl.IdxBuffer.Data, src.idx_buffer.data(), src.idx_buffer.size());
        memcpy(dl.VtxBuffer.Data, src.vtx_buffer.data(), src.vtx_buffer.size());
        draw_list_ptrs[i] = &dl;
    }

    ImDrawData draw_data;
    draw_data.Valid         = true;
    draw_data.CmdListsCount = static_cast<int>(draw_list_ptrs.size());
    draw_data.CmdLists.reserve(draw_data.CmdListsCount);
    for (auto* p : draw_list_ptrs) draw_data.CmdLists.push_back(p);
    draw_data.TotalVtxCount    = snap->total_vtx_count;
    draw_data.TotalIdxCount    = snap->total_idx_count;
    draw_data.DisplayPos       = ImVec2(snap->display_pos_x, snap->display_pos_y);
    draw_data.DisplaySize      = ImVec2(snap->display_size_x, snap->display_size_y);
    draw_data.FramebufferScale = ImVec2(snap->fb_scale_x, snap->fb_scale_y);
    // Point to the shared texture list so the backend can create/update textures.
    draw_data.Textures = &ImGui::GetPlatformIO().Textures;

    // Create a render pass targeting the primary window surface
    wgpu::CommandEncoder encoder =
        device->createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("ImGui Encoder"));
    wgpu::RenderPassEncoder pass =
        encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                    .setLabel("ImGui Pass")
                                    .setColorAttachments(std::array{
                                        wgpu::RenderPassColorAttachment()
                                            .setView(window.swapchain_texture_view)
                                            .setDepthSlice(~0u)
                                            .setLoadOp(wgpu::LoadOp::eLoad)  // preserve scene
                                            .setStoreOp(wgpu::StoreOp::eStore),
                                    }));

    ImGui_ImplWGPU_RenderDrawData(&draw_data, pass);
    pass.end();

    // Prevent the temporary draw_data from freeing draw lists it doesn't own
    draw_data.Clear();

    wgpu::CommandBuffer cmd = encoder.finish();
    queue->submit(cmd);
}
