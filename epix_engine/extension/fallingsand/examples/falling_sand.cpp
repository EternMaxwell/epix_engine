#include <imgui.h>
#ifndef EPIX_IMPORT_STD
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import webgpu;
import epix.core;
import epix.assets;
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
import epix.extension.fallingsand;
import epix.time;

using namespace epix;
using namespace epix::core;
namespace fs = epix::ext::fallingsand;

// ──────────────────────────────────────────────────────────────────────────────
// Per-element display info stored in app state.
// ──────────────────────────────────────────────────────────────────────────────
struct ElementInfo {
    std::size_t id;
    std::string name;
    glm::vec4 color;  ///< Representative colour for UI buttons.
};

// ──────────────────────────────────────────────────────────────────────────────
// Per-example state shared between systems.
// ──────────────────────────────────────────────────────────────────────────────
struct SandAppState {
    std::vector<ElementInfo> elements;
    std::size_t selected_idx  = 0;
    std::int32_t brush_radius = 2;
    Entity world_entity       = {};
    // Chunk map view state
    glm::vec2 map_pan            = {0.0f, 0.0f};  // canvas centre in chunk-space (y-down)
    float map_zoom               = 32.0f;         // pixels per chunk
    bool map_dragging            = false;
    ImVec2 map_drag_start_mouse  = {};
    glm::vec2 map_drag_start_pan = {};
    // Camera pan-drag state (middle mouse)
    bool cam_dragging              = false;
    glm::vec2 cam_drag_start_mouse = {};
    glm::vec3 cam_drag_start_pos   = {};
};

// ──────────────────────────────────────────────────────────────────────────────
// Helper: build a transient SandSimulation, or nullopt on failure.
// ──────────────────────────────────────────────────────────────────────────────
static std::optional<fs::SandSimulation> make_sim(
    fs::SandWorld& world,
    const fs::ElementRegistry& registry,
    std::optional<std::reference_wrapper<const Children>> maybe_children,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>&
        all_chunks) {
    if (!maybe_children.has_value()) return std::nullopt;
    const auto& child_entities = maybe_children->get().entities();
    auto chunk_range =
        child_entities | std::views::filter([&all_chunks](Entity e) { return all_chunks.get(e).has_value(); }) |
        std::views::transform(
            [&all_chunks](
                Entity e) -> std::tuple<ext::grid::Chunk<fs::kDim>&, const fs::SandChunkPos&, fs::SandChunkDirtyRect&> {
                auto o                                   = all_chunks.get(e);
                auto&& [chunk, pos, dirty_rect, par_ref] = *o;
                return {chunk.get_mut(), pos, dirty_rect.get_mut()};
            });
    auto result = fs::SandSimulation::create(world, registry, chunk_range);
    if (!result.has_value()) return std::nullopt;
    return std::move(*result);
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-frame imgui settings panel.
// ──────────────────────────────────────────────────────────────────────────────
void settings_ui(
    imgui::Ctx imgui_ctx,
    ResMut<SandAppState> app_state,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Res<fs::ElementRegistry> registry,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>
        all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto& w                             = sand_world.get_mut();
    auto& st                            = app_state.get_mut();

    ImGui::Begin("Falling Sand Settings");

    bool paused = w.paused();
    if (ImGui::Checkbox("Paused (Space)", &paused)) w.set_paused(paused);

    bool show_outlines = w.show_chunk_outlines();
    if (ImGui::Checkbox("Show Chunk Outlines", &show_outlines)) w.set_show_chunk_outlines(show_outlines);

    int brush = st.brush_radius;
    if (ImGui::SliderInt("Brush Radius (Q/E)", &brush, 1, 32)) st.brush_radius = static_cast<std::int32_t>(brush);

    float cell = w.cell_size();
    if (ImGui::SliderFloat("Cell Size", &cell, 1.0f, 16.0f, "%.1f")) w.set_cell_size(cell);

    ImGui::Separator();
    ImGui::Text("Element (Tab to cycle):");
    for (std::size_t i = 0; i < st.elements.size(); ++i) {
        const auto& ei = st.elements[i];
        ImVec4 col     = {ei.color.r, ei.color.g, ei.color.b, 1.0f};
        bool selected  = (i == st.selected_idx);
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, {col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
        if (ImGui::Button(ei.name.c_str())) st.selected_idx = i;
        ImGui::PopStyleColor(2);
        if (i + 1 < st.elements.size()) ImGui::SameLine();
    }

    ImGui::Separator();
    if (ImGui::Button("Clear (C)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks))
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset (R)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks)) {
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
            if (!st.elements.empty()) {
                const auto& ei   = st.elements[0];
                const auto& base = registry.get()[ei.id];
                for (std::int64_t y = -4; y <= 4; ++y)
                    for (std::int64_t x = -8; x <= 8; ++x) {
                        std::uint64_t sd = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                           static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                        (void)sim->insert_cell({x, y}, fs::Element{ei.id, base.color_func(sd)});
                    }
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("R=reset  C=clear  Q/E=brush  Space=pause  Tab=next element");
    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────────────
// Chunk map window: visualize and interactively manage chunk layout.
// ──────────────────────────────────────────────────────────────────────────────
void chunk_map_ui(imgui::Ctx imgui_ctx,
                  Commands cmd,
                  ResMut<SandAppState> app_state,
                  Query<Item<Mut<fs::SandWorld>>, With<fs::SimulatedByPlugin>> worlds,
                  Query<Item<Entity, const fs::SandChunkPos&, const Parent&>> chunk_query) {
    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto& w  = std::get<0>(*world_opt).get_mut();
    auto& st = app_state.get_mut();

    if (!ImGui::Begin("Chunk Map")) {
        ImGui::End();
        return;
    }

    // Canvas setup ─────────────────────────────────────────────────────────────
    ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 60.0f) canvas_size.x = 60.0f;
    if (canvas_size.y < 60.0f) canvas_size.y = 60.0f;
    canvas_size.y -= 40.0f;  // reserve space for status text below

    // Invisible button covers the canvas area and captures mouse events.
    ImGui::InvisibleButton(
        "map_canvas", canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvas_hovered = ImGui::IsItemHovered();

    ImVec2 canvas_end = {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y};
    ImVec2 canvas_ctr = {canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f};

    auto& io   = ImGui::GetIO();
    auto& pan  = st.map_pan;
    auto& zoom = st.map_zoom;

    // ── Zoom: scroll wheel ────────────────────────────────────────────────────
    if (canvas_hovered && io.MouseWheel != 0.0f) {
        float old_zoom = zoom;
        zoom           = std::clamp(zoom * std::pow(1.15f, io.MouseWheel), 4.0f, 256.0f);
        // Zoom toward mouse cursor
        pan.x += (io.MousePos.x - canvas_ctr.x) / old_zoom - (io.MousePos.x - canvas_ctr.x) / zoom;
        pan.y += (io.MousePos.y - canvas_ctr.y) / old_zoom - (io.MousePos.y - canvas_ctr.y) / zoom;
    }

    // ── MMB drag: pan ─────────────────────────────────────────────────────────
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        st.map_dragging         = true;
        st.map_drag_start_mouse = io.MousePos;
        st.map_drag_start_pan   = pan;
    }
    if (st.map_dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            pan.x = st.map_drag_start_pan.x - (io.MousePos.x - st.map_drag_start_mouse.x) / zoom;
            pan.y = st.map_drag_start_pan.y - (io.MousePos.y - st.map_drag_start_mouse.y) / zoom;
        } else {
            st.map_dragging = false;
        }
    }

    // ── WASD pan (canvas hovered, imgui not consuming keyboard) ───────────────
    if (canvas_hovered && !io.WantCaptureKeyboard) {
        constexpr float kPanStep = 0.5f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) pan.x -= kPanStep;
        if (ImGui::IsKeyDown(ImGuiKey_D)) pan.x += kPanStep;
        if (ImGui::IsKeyDown(ImGuiKey_W)) pan.y -= kPanStep;
        if (ImGui::IsKeyDown(ImGuiKey_S)) pan.y += kPanStep;
    }

    // ── Coordinate helpers ────────────────────────────────────────────────────
    // Map convention: y-down on screen (world cy increases up → flip sign).
    auto chunk_to_screen = [&](std::int32_t cx, std::int32_t cy) -> ImVec2 {
        return {canvas_ctr.x + (static_cast<float>(cx) - pan.x) * zoom,
                canvas_ctr.y + (static_cast<float>(-cy) - pan.y) * zoom};
    };
    auto screen_to_chunk = [&](ImVec2 pos, std::int32_t& out_cx, std::int32_t& out_cy) {
        float fx = (pos.x - canvas_ctr.x) / zoom + pan.x;
        float fy = (pos.y - canvas_ctr.y) / zoom + pan.y;
        out_cx   = static_cast<std::int32_t>(std::floor(fx));
        out_cy   = -static_cast<std::int32_t>(std::floor(fy));
    };
    auto encode_pos = [](std::int32_t cx, std::int32_t cy) -> std::int64_t {
        return (static_cast<std::int64_t>(static_cast<std::uint32_t>(cx)) << 32) |
               static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
    };

    // Build fast lookup of existing chunk positions → entity.
    std::unordered_map<std::int64_t, Entity> existing;
    for (auto&& [ent, chunk_pos, par] : chunk_query.iter())
        existing[encode_pos(chunk_pos.value[0], chunk_pos.value[1])] = ent;

    // ── LMB click: spawn new chunk ────────────────────────────────────────────
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !st.map_dragging) {
        std::int32_t cx, cy;
        screen_to_chunk(io.MousePos, cx, cy);
        if (existing.find(encode_pos(cx, cy)) == existing.end()) {
            std::size_t cs       = w.chunk_shift();
            float csize          = w.cell_size();
            std::int32_t chunk_w = static_cast<std::int32_t>(std::size_t(1) << cs);
            ext::grid::Chunk<fs::kDim> chunk(cs);
            auto layer_res = chunk.add_layer(std::make_unique<ext::grid::layers::TreeLayer<fs::kDim, fs::Element>>(cs));
            if (layer_res.has_value()) {
                cmd.entity(st.world_entity)
                    .spawn(
                        std::move(chunk), fs::SandChunkPos{{cx, cy}},
                        transform::Transform{.translation = glm::vec3(static_cast<float>(cx * chunk_w) * csize,
                                                                      static_cast<float>(cy * chunk_w) * csize, 0.0f)});
            }
        }
    }

    // ── RMB click: despawn existing chunk ─────────────────────────────────────
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        std::int32_t cx, cy;
        screen_to_chunk(io.MousePos, cx, cy);
        auto it = existing.find(encode_pos(cx, cy));
        if (it != existing.end()) cmd.entity(it->second).despawn();
    }

    // ── Draw canvas background and border ─────────────────────────────────────
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvas_pos, canvas_end, IM_COL32(20, 20, 20, 255));
    dl->AddRect(canvas_pos, canvas_end, IM_COL32(100, 100, 100, 255));
    dl->PushClipRect(canvas_pos, canvas_end, true);

    // Grid origin crosshair
    ImVec2 org = chunk_to_screen(0, 0);
    dl->AddLine({org.x, canvas_pos.y}, {org.x, canvas_end.y}, IM_COL32(60, 60, 60, 180), 1.0f);
    dl->AddLine({canvas_pos.x, org.y}, {canvas_end.x, org.y}, IM_COL32(60, 60, 60, 180), 1.0f);

    // Existing chunks
    for (auto&& [ent, chunk_pos, par] : chunk_query.iter()) {
        std::int32_t cx = chunk_pos.value[0];
        std::int32_t cy = chunk_pos.value[1];
        ImVec2 tl       = chunk_to_screen(cx, cy);
        ImVec2 br       = {tl.x + zoom, tl.y + zoom};
        dl->AddRectFilled(tl, br, IM_COL32(50, 90, 50, 200));
        dl->AddRect(tl, br, IM_COL32(100, 200, 100, 255));
        if (zoom >= 18.0f) {
            char label[24];
            std::snprintf(label, sizeof(label), "%d,%d", cx, cy);
            dl->AddText({tl.x + 2.0f, tl.y + 2.0f}, IM_COL32(200, 255, 200, 255), label);
        }
    }

    dl->PopClipRect();

    // Status text below canvas
    ImGui::SetCursorScreenPos({canvas_pos.x, canvas_end.y + 4.0f});
    ImGui::Text("LMB=add  RMB=remove  MMB/WASD=pan  Scroll=zoom  Chunks: %zu  Zoom: %.0f px", existing.size(),
                static_cast<double>(zoom));

    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────────────
// Camera control: scroll-to-zoom, MMB-drag-to-pan, arrow-key pan.
// ──────────────────────────────────────────────────────────────────────────────
void camera_control(ResMut<SandAppState> app_state,
                    EventReader<input::MouseScroll> scroll_events,
                    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                    Res<input::ButtonInput<input::KeyCode>> keys,
                    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
                    Query<Item<Mut<transform::Transform>, Mut<render::camera::Projection>>,
                          With<core_graph::core_2d::Camera2D>> cameras) {
    if (ImGui::GetIO().WantCaptureMouse && ImGui::GetIO().WantCaptureKeyboard) return;

    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam_tf, cam_proj] = *cam_opt;
    auto& tf                  = cam_tf.get_mut();
    auto& proj                = cam_proj.get_mut();
    auto& st                  = app_state.get_mut();

    auto ortho_opt = proj.as_orthographic();
    if (!ortho_opt.has_value()) return;
    auto* ortho = *ortho_opt;

    // ── Scroll-to-zoom (when ImGui is NOT capturing mouse) ────────────────────
    if (!ImGui::GetIO().WantCaptureMouse) {
        for (auto&& ev : scroll_events.read()) {
            float factor = std::pow(1.15f, -(float)ev.yoffset);
            ortho->scale = std::clamp(ortho->scale * factor, 0.05f, 50.0f);
        }
    }

    // ── MMB drag to pan ───────────────────────────────────────────────────────
    auto win_opt = windows.single();
    if (win_opt.has_value() && !ImGui::GetIO().WantCaptureMouse) {
        auto&& [win]    = *win_opt;
        auto [cx, cy]   = win.cursor_pos;
        glm::vec2 mouse = {(float)cx, (float)cy};

        if (mouse_buttons->just_pressed(input::MouseButton::MouseButtonMiddle)) {
            st.cam_dragging         = true;
            st.cam_drag_start_mouse = mouse;
            st.cam_drag_start_pos   = tf.translation;
        }
        if (st.cam_dragging) {
            if (mouse_buttons->pressed(input::MouseButton::MouseButtonMiddle)) {
                glm::vec2 delta = mouse - st.cam_drag_start_mouse;
                // pixel delta → world delta (scale maps 1 world unit to 1/scale pixels roughly)
                float s          = ortho->scale;
                tf.translation.x = st.cam_drag_start_pos.x - delta.x * s;
                tf.translation.y = st.cam_drag_start_pos.y + delta.y * s;
            } else {
                st.cam_dragging = false;
            }
        }
    }

    // ── Arrow key pan ─────────────────────────────────────────────────────────
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        constexpr float kPanSpeed = 200.0f;
        float s                   = ortho->scale;
        if (keys->pressed(input::KeyCode::KeyLeft)) tf.translation.x -= kPanSpeed * s * (1.0f / 60.0f);
        if (keys->pressed(input::KeyCode::KeyRight)) tf.translation.x += kPanSpeed * s * (1.0f / 60.0f);
        if (keys->pressed(input::KeyCode::KeyUp)) tf.translation.y += kPanSpeed * s * (1.0f / 60.0f);
        if (keys->pressed(input::KeyCode::KeyDown)) tf.translation.y -= kPanSpeed * s * (1.0f / 60.0f);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Input system: keyboard shortcuts and mouse painting.
// ──────────────────────────────────────────────────────────────────────────────
void input_system(
    ResMut<SandAppState> app_state,
    Res<fs::ElementRegistry> registry,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Res<input::ButtonInput<input::KeyCode>> keys,
    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
    Query<Item<const render::camera::Camera&, const transform::Transform&>> cameras,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>
        all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto& w                             = sand_world.get_mut();
    auto& st                            = app_state.get_mut();

    if (keys->just_pressed(input::KeyCode::KeySpace)) w.set_paused(!w.paused());
    if (keys->just_pressed(input::KeyCode::KeyQ)) st.brush_radius = std::max(1, st.brush_radius - 1);
    if (keys->just_pressed(input::KeyCode::KeyE)) st.brush_radius = std::min(32, st.brush_radius + 1);
    if (keys->just_pressed(input::KeyCode::KeyTab) && !st.elements.empty())
        st.selected_idx = (st.selected_idx + 1) % st.elements.size();

    bool need_clear = keys->just_pressed(input::KeyCode::KeyC);
    bool need_reset = keys->just_pressed(input::KeyCode::KeyR);
    bool lmb        = mouse_buttons->pressed(input::MouseButton::MouseButtonLeft);
    bool rmb        = mouse_buttons->pressed(input::MouseButton::MouseButtonRight);

    if (!need_clear && !need_reset && !lmb && !rmb) return;

    auto sim = make_sim(w, registry.get(), maybe_children, all_chunks);
    if (!sim.has_value()) return;

    if (need_clear)
        for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();

    if (need_reset) {
        for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
        if (!st.elements.empty()) {
            const auto& ei   = st.elements[0];
            const auto& base = registry.get()[ei.id];
            for (std::int64_t y = -4; y <= 4; ++y)
                for (std::int64_t x = -8; x <= 8; ++x) {
                    std::uint64_t sd = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                       static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                    (void)sim->insert_cell({x, y}, fs::Element{ei.id, base.color_func(sd)});
                }
        }
    }

    if ((lmb || rmb) && !ImGui::GetIO().WantCaptureMouse) {
        auto win_opt = windows.single();
        auto cam_opt = cameras.single();
        if (!win_opt.has_value() || !cam_opt.has_value()) return;

        auto&& [primary_window]     = *win_opt;
        auto&& [cam, cam_transform] = *cam_opt;
        auto [rel_x, rel_y]         = primary_window.relative_cursor_pos();
        glm::vec2 world_cursor =
            fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_transform);

        float cs         = w.cell_size();
        std::int32_t cx_ = static_cast<std::int32_t>(std::floor(world_cursor.x / cs));
        std::int32_t cy_ = static_cast<std::int32_t>(std::floor(world_cursor.y / cs));
        std::int32_t br  = st.brush_radius;

        if (lmb && !st.elements.empty()) {
            const auto& ei   = st.elements[st.selected_idx];
            const auto& base = registry.get()[ei.id];
            std::int32_t r2  = br * br;
            for (std::int32_t dy = -br; dy <= br; ++dy)
                for (std::int32_t dx = -br; dx <= br; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    std::int32_t px = cx_ + dx, py = cy_ + dy;
                    std::uint64_t sd = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32) |
                                       static_cast<std::uint64_t>(static_cast<std::uint32_t>(py));
                    (void)sim->insert_cell({px, py}, fs::Element{ei.id, base.color_func(sd)});
                }
        }
        if (rmb) {
            std::int32_t r2 = br * br;
            for (std::int32_t dy = -br; dy <= br; ++dy)
                for (std::int32_t dx = -br; dx <= br; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    (void)sim->remove_cell<fs::Element>({cx_ + dx, cy_ + dy});
                }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Startup: seed an initial sand pile once chunks exist.
// ──────────────────────────────────────────────────────────────────────────────
void seed(
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Res<fs::ElementRegistry> registry,
    Res<SandAppState> app_state,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>
        all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto sim                            = make_sim(sand_world.get_mut(), registry.get(), maybe_children, all_chunks);
    if (!sim.has_value()) return;
    const auto& st = app_state.get();
    if (st.elements.empty()) return;
    const auto& ei   = st.elements[0];  // seed with sand
    const auto& base = registry.get()[ei.id];
    for (std::int64_t y = -4; y <= 4; ++y)
        for (std::int64_t x = -8; x <= 8; ++x) {
            std::uint64_t sd = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                               static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
            (void)sim->insert_cell({x, y}, fs::Element{ei.id, base.color_func(sd)});
        }
}

// ──────────────────────────────────────────────────────────────────────────────
// Startup: register elements, spawn camera, world entity, and initial chunks.
// ──────────────────────────────────────────────────────────────────────────────
void setup(Commands cmd) {
    fs::ElementRegistry registry;
    SandAppState st;

    // Helper: register one element and push its info into st.elements.
    auto reg = [&](fs::ElementBase base, glm::vec4 repr_color) {
        auto res = registry.register_element(std::move(base));
        if (res.has_value()) st.elements.push_back({*res, registry[*res].name, repr_color});
    };

    // ── Sand (Powder) ─────────────────────────────────────────────────────────
    reg(
        fs::ElementBase{
            .name    = "sand",
            .density = 1.5f,
            .type    = fs::ElementType::Powder,
            .color_func =
                [](std::uint64_t sd) {
                    sd += 0x9e3779b97f4a7c15ULL;
                    sd = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    sd = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    sd ^= (sd >> 31);
                    float t = static_cast<float>(sd) / static_cast<float>(std::numeric_limits<std::uint64_t>::max());
                    return glm::vec4(0.80f + t * 0.18f, 0.68f + t * 0.15f, 0.18f + t * 0.08f, 1.0f);
                },
        },
        glm::vec4(0.80f, 0.68f, 0.18f, 1.0f));

    // ── Water (Liquid) ────────────────────────────────────────────────────────
    reg(
        fs::ElementBase{
            .name        = "water",
            .density     = 1.0f,
            .type        = fs::ElementType::Liquid,
            .restitution = 0.05f,
            .friction    = 0.1f,
            .awake_rate  = 1.0f,
            .color_func =
                [](std::uint64_t sd) {
                    sd += 0x6c62272e07bb0142ULL;
                    sd = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    sd ^= (sd >> 31);
                    float t = static_cast<float>(sd & 0xFFFF) / 65535.0f;
                    return glm::vec4(0.15f + t * 0.15f, 0.45f + t * 0.15f, 0.85f + t * 0.12f, 1.0f);
                },
        },
        glm::vec4(0.20f, 0.50f, 0.90f, 1.0f));

    // ── Stone (Solid) ─────────────────────────────────────────────────────────
    reg(
        fs::ElementBase{
            .name    = "stone",
            .density = 5.0f,
            .type    = fs::ElementType::Solid,
            .color_func =
                [](std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float v = 0.40f + t * 0.20f;
                    return glm::vec4(v, v, v, 1.0f);
                },
        },
        glm::vec4(0.50f, 0.50f, 0.50f, 1.0f));

    // ── Smoke (Gas) ───────────────────────────────────────────────────────────
    reg(
        fs::ElementBase{
            .name       = "smoke",
            .density    = 0.001f,
            .type       = fs::ElementType::Gas,
            .awake_rate = 1.0f,
            .color_func =
                [](std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float v = 0.55f + t * 0.25f;
                    return glm::vec4(v, v, v, 0.8f);
                },
        },
        glm::vec4(0.70f, 0.70f, 0.70f, 0.8f));

    cmd.insert_resource(std::move(registry));
    cmd.spawn(core_graph::core_2d::Camera2DBundle{});

    constexpr std::size_t chunk_shift = 5;
    constexpr float cell_size         = 4.0f;
    constexpr std::int32_t chunk_w    = static_cast<std::int32_t>(std::size_t(1) << chunk_shift);
    constexpr std::int32_t chunks_x   = 4;
    constexpr std::int32_t chunks_y   = 3;

    auto world_cmds = cmd.spawn(fs::SandWorld(chunk_shift, cell_size), transform::Transform{}, fs::SimulatedByPlugin{},
                                fs::MeshBuildByPlugin{});
    st.world_entity = world_cmds.id();

    for (std::int32_t cy = -chunks_y; cy < chunks_y; ++cy) {
        for (std::int32_t cx = -chunks_x; cx < chunks_x; ++cx) {
            ext::grid::Chunk<fs::kDim> chunk(chunk_shift);
            auto layer_res =
                chunk.add_layer(std::make_unique<ext::grid::layers::TreeLayer<fs::kDim, fs::Element>>(chunk_shift));
            if (!layer_res.has_value()) continue;
            world_cmds.spawn(
                std::move(chunk), fs::SandChunkPos{{cx, cy}},
                transform::Transform{.translation = glm::vec3(static_cast<float>(cx * chunk_w) * cell_size,
                                                              static_cast<float>(cy * chunk_w) * cell_size, 0.0f)});
        }
    }

    cmd.insert_resource(std::move(st));
}

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Falling Sand  |  LMB paint  RMB erase  Tab cycle element  Space pause";
    primary_window.size  = {1280, 720};

    app.add_plugins(core::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
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
        .add_plugins(time::TimePlugin{})
        .add_plugins(fs::FallingSandPlugin{})
        .add_systems(PreStartup, into(setup).set_name("fallingsand example setup"))
        .add_systems(Startup, into(seed).set_name("fallingsand example seed"))
        .add_systems(PreUpdate, into(settings_ui).after(imgui::BeginFrameSet))
        .add_systems(PreUpdate, into(chunk_map_ui).after(imgui::BeginFrameSet))
        .add_systems(Update, into(input_system))
        .add_systems(Update, into(camera_control));

    app.run();
}
