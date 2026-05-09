module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#endif
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
import epix.glfw.render;
import webgpu;
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix;
using namespace epix::core;

namespace win  = ::epix::window;
namespace rwin = epix::render::window;

namespace {
bool g_wgpu_initialized = false;

struct ReconstructedDrawData {
    std::vector<ImDrawList> draw_lists_storage;
    std::vector<ImDrawList*> draw_list_ptrs;
    ImDrawData draw_data;

    explicit ReconstructedDrawData(const epix::imgui::DrawDataSnapshot& snap)
        : draw_lists_storage(snap.draw_lists.size(), ImDrawList(nullptr)), draw_list_ptrs(snap.draw_lists.size()) {
        for (size_t i = 0; i < snap.draw_lists.size(); i++) {
            const auto& src = snap.draw_lists[i];
            ImDrawList& dl  = draw_lists_storage[i];
            dl.CmdBuffer.resize(src.cmd_count);
            dl.IdxBuffer.resize(src.idx_count);
            dl.VtxBuffer.resize(src.vtx_count);
            memcpy(dl.CmdBuffer.Data, src.cmd_buffer.data(), src.cmd_buffer.size());
            memcpy(dl.IdxBuffer.Data, src.idx_buffer.data(), src.idx_buffer.size());
            memcpy(dl.VtxBuffer.Data, src.vtx_buffer.data(), src.vtx_buffer.size());
            draw_list_ptrs[i] = &dl;
        }

        draw_data.Valid         = true;
        draw_data.CmdListsCount = static_cast<int>(draw_list_ptrs.size());
        draw_data.CmdLists.reserve(draw_data.CmdListsCount);
        for (auto* draw_list : draw_list_ptrs) draw_data.CmdLists.push_back(draw_list);
        draw_data.TotalVtxCount    = snap.total_vtx_count;
        draw_data.TotalIdxCount    = snap.total_idx_count;
        draw_data.DisplayPos       = ImVec2(snap.display_pos_x, snap.display_pos_y);
        draw_data.DisplaySize      = ImVec2(snap.display_size_x, snap.display_size_y);
        draw_data.FramebufferScale = ImVec2(snap.fb_scale_x, snap.fb_scale_y);
        draw_data.Textures         = &ImGui::GetPlatformIO().Textures;
    }
};

struct ViewportSurfaceData {
    wgpu::Surface surface;
    wgpu::SurfaceConfiguration config;
    int width  = 0;
    int height = 0;
};

std::unordered_map<unsigned int, ViewportSurfaceData> g_viewport_surfaces;

bool clone_draw_data(ImDrawData* dd, epix::imgui::DrawDataSnapshot& snap) {
    if (!dd || dd->CmdLists.Size <= 0) return false;

    snap.valid           = true;
    snap.display_pos_x   = dd->DisplayPos.x;
    snap.display_pos_y   = dd->DisplayPos.y;
    snap.display_size_x  = dd->DisplaySize.x;
    snap.display_size_y  = dd->DisplaySize.y;
    snap.fb_scale_x      = dd->FramebufferScale.x;
    snap.fb_scale_y      = dd->FramebufferScale.y;
    snap.total_vtx_count = dd->TotalVtxCount;
    snap.total_idx_count = dd->TotalIdxCount;
    snap.draw_lists.resize(dd->CmdLists.Size);
    for (int i = 0; i < dd->CmdLists.Size; i++) {
        const ImDrawList* src = dd->CmdLists[i];
        auto& dst             = snap.draw_lists[i];
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
    return true;
}

bool can_use_platform_viewports(const ImGuiIO& io) {
    return (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) &&
           (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) &&
           (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports);
}

bool surface_supports_format(wgpu::SurfaceCapabilities& capabilities, wgpu::TextureFormat format) {
    for (auto available : capabilities.formats) {
        if (available == format) return true;
    }
    return false;
}

ViewportSurfaceData* ensure_viewport_surface(const epix::imgui::ViewportDrawDataSnapshot& snap,
                                             const wgpu::TextureFormat render_target_format,
                                             const wgpu::Instance& instance,
                                             const wgpu::Adapter& adapter,
                                             const wgpu::Device& device) {
    if (!snap.platform_handle) return nullptr;
    GLFWwindow* glfw_window = static_cast<GLFWwindow*>(snap.platform_handle);

    // Use the framebuffer size captured on the main thread — calling
    // glfwGetFramebufferSize from the render thread is unsafe.
    int width  = snap.fb_width;
    int height = snap.fb_height;
    if (width <= 0 || height <= 0) return nullptr;

    auto it = g_viewport_surfaces.find(snap.viewport_id);
    if (it == g_viewport_surfaces.end()) {
        wgpu::Surface surface = epix::glfw::render::get_wgpu_surface(instance, glfw_window);
        wgpu::SurfaceCapabilities capabilities;
        auto status = surface.getCapabilities(adapter, &capabilities);
        if (status != wgpu::Status::eSuccess) {
            spdlog::warn("[imgui] Failed to get surface capabilities for viewport {}.", snap.viewport_id);
            return nullptr;
        }
        if (!surface_supports_format(capabilities, render_target_format)) {
            spdlog::warn("[imgui] Viewport {} surface does not support primary ImGui render target format {}.",
                         snap.viewport_id, static_cast<int>(render_target_format));
            return nullptr;
        }

        auto config = wgpu::SurfaceConfiguration()
                          .setDevice(device)
                          .setUsage(wgpu::TextureUsage::eRenderAttachment)
                          .setFormat(render_target_format)
                          .setWidth(width)
                          .setHeight(height)
                          .setPresentMode(wgpu::PresentMode::eFifo)
                          .setAlphaMode(wgpu::CompositeAlphaMode::eAuto);
        surface.configure(config);
        it = g_viewport_surfaces
                 .emplace(snap.viewport_id, ViewportSurfaceData{std::move(surface), config, width, height})
                 .first;
        spdlog::debug("[imgui] Created platform viewport surface {} ({}x{}).", snap.viewport_id, width, height);
    } else if (it->second.width != width || it->second.height != height) {
        it->second.width  = width;
        it->second.height = height;
        it->second.config.setWidth(width);
        it->second.config.setHeight(height);
        it->second.surface.configure(it->second.config);
        spdlog::trace("[imgui] Reconfigured platform viewport surface {} ({}x{}).", snap.viewport_id, width, height);
    }

    return &it->second;
}

void render_viewport_snapshot(const epix::imgui::ViewportDrawDataSnapshot& snap,
                              const wgpu::TextureFormat render_target_format,
                              const wgpu::Instance& instance,
                              const wgpu::Adapter& adapter,
                              const wgpu::Device& device,
                              const wgpu::Queue& queue) {
    if (!snap.valid || snap.minimized || snap.draw_lists.empty()) return;
    if (snap.display_size_x <= 0.0f || snap.display_size_y <= 0.0f) return;

    ViewportSurfaceData* surface_data = ensure_viewport_surface(snap, render_target_format, instance, adapter, device);
    if (!surface_data) return;

    wgpu::SurfaceTexture surface_texture;
    surface_data->surface.getCurrentTexture(&surface_texture);
    switch (surface_texture.status) {
        case wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal:
        case wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal:
            break;
        case wgpu::SurfaceGetCurrentTextureStatus::eOutdated:
            surface_data->surface.configure(surface_data->config);
            surface_data->surface.getCurrentTexture(&surface_texture);
            if (surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal &&
                surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal) {
                spdlog::warn("[imgui] Failed to acquire viewport {} swapchain texture after reconfigure: {}.",
                             snap.viewport_id, wgpu::to_string(surface_texture.status));
                return;
            }
            break;
        default:
            spdlog::warn("[imgui] Failed to acquire viewport {} swapchain texture: {}.", snap.viewport_id,
                         wgpu::to_string(surface_texture.status));
            return;
    }

    wgpu::TextureView texture_view = surface_texture.texture.createView();
    ReconstructedDrawData reconstructed(snap);

    wgpu::CommandEncoder encoder =
        device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("ImGui Platform Viewport Encoder"));
    wgpu::RenderPassEncoder pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                               .setLabel("ImGui Platform Viewport Pass")
                                                               .setColorAttachments(std::array{
                                                                   wgpu::RenderPassColorAttachment()
                                                                       .setView(texture_view)
                                                                       .setDepthSlice(~0u)
                                                                       .setLoadOp(wgpu::LoadOp::eClear)
                                                                       .setStoreOp(wgpu::StoreOp::eStore)
                                                                       .setClearValue(wgpu::Color{0.0, 0.0, 0.0, 1.0}),
                                                               }));
    ImGui_ImplWGPU_RenderDrawData(&reconstructed.draw_data, pass);
    pass.end();
    reconstructed.draw_data.Clear();

    wgpu::CommandBuffer cmd = encoder.finish();
    queue.submit(cmd);
    surface_data->surface.present();
}
}  // namespace

imgui::ImGuiPlugin& imgui::ImGuiPlugin::set_docking(bool enabled) noexcept {
    enable_docking = enabled;
    return *this;
}

imgui::ImGuiPlugin& imgui::ImGuiPlugin::set_viewports(bool enabled) noexcept {
    enable_viewports = enabled;
    return *this;
}

void imgui::ImGuiPlugin::build(App& app) {
    spdlog::debug("[imgui] Building ImGuiPlugin.");
    // Create ImGui context and store in main world
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (enable_docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (enable_viewports) {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
    }
    // Tell ImGui that the renderer will handle texture creation on-demand.
    // This must be set BEFORE any NewFrame() call so that the font atlas
    // build takes the new (non-legacy) path and avoids preloading all glyphs.
    // The WebGPU backend also sets this flag during Init(), which is a no-op
    // since the flag is already present.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    app.world_mut().insert_resource(ImGuiState{
        .ctx              = ctx,
        .initialized      = false,
        .enable_docking   = enable_docking,
        .enable_viewports = enable_viewports,
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

void imgui::ImGuiPlugin::finalize(App&) {
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        if (enable_viewports) ImGui::DestroyPlatformWindows();
        if (g_wgpu_initialized) {
            ImGui_ImplWGPU_Shutdown();
            g_wgpu_initialized = false;
        }
        if (io.BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    g_viewport_surfaces.clear();
}

void imgui::imgui_begin_frame(ResMut<ImGuiState> state,
                              Res<glfw::GLFWwindows> windows,
                              Query<Item<Entity>, With<win::Window, win::PrimaryWindow>> primary) {
    if (!state->ctx) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(state->ctx));

    // Lazy-initialize backends on first frame when the primary window is available
    if (!state->initialized) {
        spdlog::trace("[imgui] Attempting lazy initialization of GLFW backend.");
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

        ImGuiIO& io = ImGui::GetIO();
        if (state->enable_viewports && !(io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports)) {
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            spdlog::warn("[imgui] Platform viewports requested, but the GLFW backend did not enable them.");
        }

        state->initialized = true;
        spdlog::debug("[imgui] Initialized GLFW backend.");
    }

    if (state->initialized) {
        // Find the primary GLFW window to check if it's still valid
        std::optional<Entity> primary_entity;
        for (auto&& [entity] : primary.iter()) {
            primary_entity = entity;
            break;
        }
        if (primary_entity) {
            auto it = windows->find(*primary_entity);
            if (it != windows->end() && !glfwWindowShouldClose(it->second)) {
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                state->frame_active = true;
            }
        }
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
    auto snap      = std::make_shared<DrawDataSnapshot>();
    if (clone_draw_data(dd, *snap)) {
        state->draw_snapshot = std::move(snap);
    } else {
        state->draw_snapshot.reset();
    }

    ImGuiIO& io = ImGui::GetIO();
    if (state->enable_viewports && can_use_platform_viewports(io)) {
        ImGui::UpdatePlatformWindows();
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        auto viewport_snaps          = std::make_shared<std::vector<ViewportDrawDataSnapshot>>();
        viewport_snaps->reserve(platform_io.Viewports.Size > 1 ? platform_io.Viewports.Size - 1 : 0);
        for (int i = 1; i < platform_io.Viewports.Size; i++) {
            ImGuiViewport* viewport = platform_io.Viewports[i];
            if (!viewport->DrawData) continue;
            ViewportDrawDataSnapshot viewport_snap;
            viewport_snap.viewport_id     = viewport->ID;
            viewport_snap.platform_handle = viewport->PlatformHandle;
            viewport_snap.minimized       = (viewport->Flags & ImGuiViewportFlags_IsMinimized) != 0;
            // Capture framebuffer size here on the main thread — GLFW must not
            // be called from the render thread (thread-safety).
            if (viewport->PlatformHandle) {
                GLFWwindow* win = static_cast<GLFWwindow*>(viewport->PlatformHandle);
                glfwGetFramebufferSize(win, &viewport_snap.fb_width, &viewport_snap.fb_height);
            }
            clone_draw_data(viewport->DrawData, viewport_snap);
            viewport_snaps->push_back(std::move(viewport_snap));
        }
        state->viewport_snapshots = std::move(viewport_snaps);
    } else {
        state->viewport_snapshots.reset();
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
                         Res<wgpu::Instance> instance,
                         Res<wgpu::Adapter> adapter,
                         Res<wgpu::Device> device,
                         Res<wgpu::Queue> queue) {
    if (!state->ctx) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(state->ctx));

    auto snap = state->draw_snapshot;
    const bool has_primary_content =
        snap && snap->valid && !snap->draw_lists.empty() && snap->display_size_x > 0.0f && snap->display_size_y > 0.0f;
    const bool has_secondary_content =
        state->enable_viewports && state->viewport_snapshots && !state->viewport_snapshots->empty();
    if (!has_primary_content && !has_secondary_content) return;

    // Find the primary window with a valid swapchain texture view (needed for WebGPU init format)
    const render::window::ExtractedWindow* target_window = nullptr;
    if (windows->primary) {
        if (auto it = windows->windows.find(*windows->primary); it != windows->windows.end()) {
            if (it->second.swapchain_texture_view) {
                target_window = &it->second;
            }
        }
    }
    if (!target_window) return;
    const auto& window = *target_window;

    // Lazy-initialize WebGPU backend
    if (!g_wgpu_initialized) {
        ImGui_ImplWGPU_InitInfo init_info{};
        init_info.Device             = *device;
        init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(window.swapchain_texture_format);
        init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
        init_info.NumFramesInFlight  = 3;
        ImGui_ImplWGPU_Init(&init_info);
        if (state->enable_viewports) ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
        g_wgpu_initialized = true;
        spdlog::debug("[imgui] Initialized WebGPU backend (format={}).",
                      static_cast<int>(window.swapchain_texture_format));
    }
    ImGui_ImplWGPU_NewFrame();

    // Primary viewport: render only if we have content and the window size is valid
    if (has_primary_content) {
        bool skip_primary = false;
        // Guard: if window physical size doesn't match snapshot display size (mid-resize),
        // skip this frame to avoid WebGPU viewport validation errors.
        if (window.physical_width > 0 && window.physical_height > 0) {
            float expected_w = snap->display_size_x * snap->fb_scale_x;
            float expected_h = snap->display_size_y * snap->fb_scale_y;
            float actual_w   = static_cast<float>(window.physical_width);
            float actual_h   = static_cast<float>(window.physical_height);
            if (expected_w > actual_w + 1.0f || expected_h > actual_h + 1.0f) skip_primary = true;
        }

        if (!skip_primary) {
            ReconstructedDrawData reconstructed(*snap);

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

            ImGui_ImplWGPU_RenderDrawData(&reconstructed.draw_data, pass);
            pass.end();

            // Prevent the temporary draw_data from freeing draw lists it doesn't own
            reconstructed.draw_data.Clear();

            wgpu::CommandBuffer cmd = encoder.finish();
            queue->submit(cmd);
        }
    }

    if (state->enable_viewports && state->viewport_snapshots) {
        std::unordered_set<unsigned int> active_viewports;
        for (const auto& viewport_snap : *state->viewport_snapshots) {
            active_viewports.insert(viewport_snap.viewport_id);
            render_viewport_snapshot(viewport_snap, window.swapchain_texture_format, *instance, *adapter, *device,
                                     *queue);
        }
        for (auto it = g_viewport_surfaces.begin(); it != g_viewport_surfaces.end();) {
            if (!active_viewports.contains(it->first)) {
                it = g_viewport_surfaces.erase(it);
            } else {
                ++it;
            }
        }
    }
}
