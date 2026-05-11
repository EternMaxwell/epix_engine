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

/** @brief Marker: identifies the primary (main-window) camera entity. */
struct MainCamera {};

/** @brief Marker: identifies a dirty-rect overlay mesh entity. */
struct DirtyRectOverlay {};

/** @brief Marker: identifies a freefall-cells overlay mesh entity. */
struct FreefallOverlay {};

/** @brief Marker: identifies the debug-window camera entity. */
struct DebugCamera {};

// ──────────────────────────────────────────────────────────────────────────────
// Per-example state shared between systems.
// ──────────────────────────────────────────────────────────────────────────────
enum class OpMode { Paint, Erase, Heat, Cool };

struct SandAppState {
    std::vector<ElementInfo> elements;
    std::size_t selected_idx  = 0;
    std::int32_t brush_radius = 2;
    Entity world_entity       = {};
    // Operation mode
    OpMode op_mode    = OpMode::Paint;
    float heat_energy = 200.0f;  ///< Joules per cell for Heat/Cool operations.
    // Debug window / dirty-rect overlay state
    Entity debug_window_entity = {};
    bool show_dirty_rects      = false;
    bool show_freefall         = false;
    std::unordered_map<Entity, Entity> dirty_rect_overlays;  // chunk_entity → overlay_entity
    std::unordered_map<Entity, Entity> freefall_overlays;    // chunk_entity → overlay_entity
    // Explosion test parameters
    float explosion_intensity  = 300.0f;
    int explosion_blast_radius = 10;  ///< Inner radius — cells removed.
    int explosion_push_radius  = 25;  ///< Outer radius — cells receive outward impulse.

    // Camera pan-drag state (middle mouse)
    bool cam_dragging              = false;
    glm::vec2 cam_drag_start_mouse = {};
    glm::vec3 cam_drag_start_pos   = {};
    // Debug camera pan-drag state
    bool debug_cam_dragging              = false;
    glm::vec2 debug_cam_drag_start_mouse = {};
    glm::vec3 debug_cam_drag_start_pos   = {};
};

// ──────────────────────────────────────────────────────────────────────────────
// Helper: build a transient SandSimulation, or nullopt on failure.
// ──────────────────────────────────────────────────────────────────────────────
static std::optional<fs::SandSimulation> make_sim(fs::SandWorld& world,
                                                  const fs::ElementRegistry& registry,
                                                  std::optional<std::reference_wrapper<const Children>> maybe_children,
                                                  Query<Item<Mut<fs::ChunkElementGrid>,
                                                             Mut<fs::ChunkAirGrid>,
                                                             Mut<fs::ChunkThermalGrid>,
                                                             const fs::SandChunkPos&,
                                                             Mut<fs::SandChunkDirtyRect>,
                                                             const Parent&>>& all_chunks) {
    if (!maybe_children.has_value()) return std::nullopt;
    const auto& child_entities = maybe_children->get().entities();
    const std::size_t shift    = world.chunk_shift();

    namespace grid = epix::ext::grid;
    std::vector<std::tuple<grid::Chunk<fs::kDim>, const fs::SandChunkPos&, fs::SandChunkDirtyRect&>> chunk_tuples;
    for (Entity e : child_entities) {
        auto opt = all_chunks.get(e);
        if (!opt.has_value()) continue;
        auto&& [elem_g, air_g, therm_g, pos, dirty_rect, par_ref] = *opt;
        grid::Chunk<fs::kDim> chunk(shift);
        (void)chunk.add_layer(
            std::make_unique<grid::layers::BasicGridRefLayer<fs::ChunkElementGrid>>(shift, std::move(elem_g)));
        (void)chunk.add_layer(
            std::make_unique<grid::layers::BasicGridRefLayer<fs::ChunkAirGrid>>(shift, std::move(air_g)));
        (void)chunk.add_layer(
            std::make_unique<grid::layers::BasicGridRefLayer<fs::ChunkThermalGrid>>(shift, std::move(therm_g)));
        chunk_tuples.emplace_back(std::move(chunk), pos, dirty_rect.get_mut());
    }
    auto chunk_range = chunk_tuples | std::views::as_rvalue;
    auto result      = fs::SandSimulation::create(world, registry, chunk_range);
    if (!result.has_value()) return std::nullopt;
    return std::move(*result);
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-frame imgui settings panel.
// ──────────────────────────────────────────────────────────────────────────────
void settings_ui(imgui::Ctx imgui_ctx,
                 ResMut<SandAppState> app_state,
                 Query<Item<Mut<fs::SandWorld>, Opt<const Children&>, Opt<Mut<fs::SandWorldDebug>>>,
                       With<fs::SimulatedByPlugin>> worlds,
                 Res<fs::ElementRegistry> registry,
                 Query<Item<Mut<fs::ChunkElementGrid>,
                            Mut<fs::ChunkAirGrid>,
                            Mut<fs::ChunkThermalGrid>,
                            const fs::SandChunkPos&,
                            Mut<fs::SandChunkDirtyRect>,
                            const Parent&>> all_chunks,
                 Query<Item<Mut<render::camera::RenderLayer>>, Filter<With<core_graph::core_2d::Camera2D, MainCamera>>>
                     main_cameras) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children, maybe_debug] = *opt;
    auto& w                                          = sand_world.get_mut();
    auto& st                                         = app_state.get_mut();

    ImGui::Begin("Falling Sand Settings");

    bool paused = w.paused();
    if (ImGui::Checkbox("Paused (Space)", &paused)) w.set_paused(paused);

    bool show_outlines = maybe_debug.has_value() && maybe_debug->get().show_chunk_outlines;
    if (ImGui::Checkbox("Show Chunk Outlines", &show_outlines)) {
        if (maybe_debug.has_value()) maybe_debug->get_mut().show_chunk_outlines = show_outlines;
    }
    bool show_heat = maybe_debug.has_value() && maybe_debug->get().show_heat_map;
    if (ImGui::Checkbox("Show Heat Map", &show_heat)) {
        if (maybe_debug.has_value()) maybe_debug->get_mut().show_heat_map = show_heat;
    }

    bool& show_dirty = st.show_dirty_rects;
    if (ImGui::Checkbox("Show Dirty Rects (debug window)", &show_dirty)) {
        if (auto cam_opt = main_cameras.single()) {
            auto&& [render_layer]  = *cam_opt;
            render_layer.get_mut() = show_dirty ? render::camera::RenderLayer::all()
                                                : render::camera::RenderLayer::all_except(std::array{std::size_t{1}});
        }
    }
    ImGui::Checkbox("Show Freefall", &st.show_freefall);
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
    ImGui::Text("Operation mode (LMB):");
    const char* mode_names[] = {"Paint", "Erase", "Heat", "Cool"};
    int mode_idx             = static_cast<int>(st.op_mode);
    if (ImGui::Combo("##opmode", &mode_idx, mode_names, 4)) st.op_mode = static_cast<OpMode>(mode_idx);

    if (st.op_mode == OpMode::Heat || st.op_mode == OpMode::Cool) {
        ImGui::SliderFloat("Heat Energy (J/cell)", &st.heat_energy, 1.0f, 10000.0f, "%.0f");
    }

    ImGui::Separator();
    if (ImGui::Button("Clear (C)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks))
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset (R)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks)) {
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.clear();
            if (!st.elements.empty()) {
                const auto& ei = st.elements[0];
                sim->apply(fs::ops::Spawn::rect({-8, -4}, {8, 4}, ei.id));
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("R=reset  C=clear  Q/E=brush  Space=pause  Tab=next element");

    ImGui::Separator();
    ImGui::Text("Explosion (F key):");
    ImGui::SliderFloat("Intensity##expl", &st.explosion_intensity, 10.0f, 5000.0f, "%.0f");
    ImGui::SliderInt("Blast Radius##expl", &st.explosion_blast_radius, 1, 64);
    ImGui::SliderInt("Push Radius##expl", &st.explosion_push_radius, 1, 128);

    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────────────
// Camera control: scroll-to-zoom, MMB-drag-to-pan, arrow-key pan.
// ──────────────────────────────────────────────────────────────────────────────
void camera_control(ResMut<SandAppState> app_state,
                    EventReader<input::MouseScroll> scroll_events,
                    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                    Res<input::ButtonInput<input::KeyCode>> keys,
                    Query<Item<Entity, const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
                    Query<Item<Mut<transform::Transform>, Mut<render::camera::Projection>>,
                          Filter<With<core_graph::core_2d::Camera2D, MainCamera>>> cameras) {
    if (ImGui::GetIO().WantCaptureMouse && ImGui::GetIO().WantCaptureKeyboard) return;

    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [win_ent, win] = *win_opt;

    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam_tf, cam_proj] = *cam_opt;
    auto& tf                  = cam_tf.get_mut();
    auto& proj                = cam_proj.get_mut();
    auto& st                  = app_state.get_mut();

    auto ortho_opt = proj.as_orthographic();
    if (!ortho_opt.has_value()) return;
    auto* ortho = *ortho_opt;

    // ── Scroll-to-zoom (always active while mouse is over window) ─────────────
    if (!ImGui::GetIO().WantCaptureMouse) {
        for (auto&& ev : scroll_events.read()) {
            if (ev.window != win_ent) continue;
            float factor = std::pow(1.15f, -(float)ev.yoffset);
            ortho->scale = std::clamp(ortho->scale * factor, 0.001f, 50.0f);
        }
    }

    // Remaining input only when window has OS focus.
    if (!win.focused) return;

    // ── MMB drag to pan ───────────────────────────────────────────────────────
    if (!ImGui::GetIO().WantCaptureMouse) {
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
// Debug camera control: scroll-to-zoom and MMB-drag pan for the debug window.
// ──────────────────────────────────────────────────────────────────────────────
void debug_camera_control(ResMut<SandAppState> app_state,
                          EventReader<input::MouseScroll> scroll_events,
                          EventReader<input::MouseButtonInput> mouse_button_events,
                          Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                          Query<Item<const window::CachedWindow&>> all_windows,
                          Query<Item<Mut<transform::Transform>, Mut<render::camera::Projection>>,
                                Filter<With<core_graph::core_2d::Camera2D, DebugCamera>>> cameras) {
    auto& st           = app_state.get_mut();
    auto debug_win_ent = st.debug_window_entity;

    // Collect events relevant to the debug window (advance reader regardless of focus).
    bool mmb_just_pressed = false;
    float scroll_delta    = 0.0f;
    for (auto&& ev : mouse_button_events.read()) {
        if (ev.window != debug_win_ent) continue;
        if (ev.button == input::MouseButton::MouseButtonMiddle) {
            if (ev.pressed)
                mmb_just_pressed = true;
            else
                st.debug_cam_dragging = false;
        }
    }
    for (auto&& ev : scroll_events.read()) {
        if (ev.window != debug_win_ent) continue;
        scroll_delta += static_cast<float>(ev.yoffset);
    }

    // Only act when the debug window has OS focus (except scroll-to-zoom).
    auto win_opt = all_windows.get(debug_win_ent);
    if (!win_opt.has_value()) return;
    auto&& [win] = *win_opt;

    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam_tf, cam_proj] = *cam_opt;
    auto& tf                  = cam_tf.get_mut();
    auto& proj                = cam_proj.get_mut();

    auto ortho_opt = proj.as_orthographic();
    if (!ortho_opt.has_value()) return;
    auto* ortho = *ortho_opt;

    // Scroll-to-zoom works even without focus.
    if (scroll_delta != 0.0f) {
        float factor = std::pow(1.15f, -scroll_delta);
        ortho->scale = std::clamp(ortho->scale * factor, 0.001f, 50.0f);
    }

    if (!win.focused) return;

    // MMB drag-to-pan.
    auto [cx, cy]   = win.cursor_pos;
    glm::vec2 mouse = {(float)cx, (float)cy};
    if (mmb_just_pressed) {
        st.debug_cam_dragging         = true;
        st.debug_cam_drag_start_mouse = mouse;
        st.debug_cam_drag_start_pos   = tf.translation;
    }
    if (st.debug_cam_dragging) {
        if (mouse_buttons->pressed(input::MouseButton::MouseButtonMiddle)) {
            glm::vec2 delta  = mouse - st.debug_cam_drag_start_mouse;
            float s          = ortho->scale;
            tf.translation.x = st.debug_cam_drag_start_pos.x - delta.x * s;
            tf.translation.y = st.debug_cam_drag_start_pos.y + delta.y * s;
        } else {
            st.debug_cam_dragging = false;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Input system: keyboard shortcuts and mouse painting.
// ──────────────────────────────────────────────────────────────────────────────
void input_system(ResMut<SandAppState> app_state,
                  Res<fs::ElementRegistry> registry,
                  Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
                  Res<input::ButtonInput<input::KeyCode>> keys,
                  Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                  Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
                  Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras,
                  Query<Item<Mut<fs::ChunkElementGrid>,
                             Mut<fs::ChunkAirGrid>,
                             Mut<fs::ChunkThermalGrid>,
                             const fs::SandChunkPos&,
                             Mut<fs::SandChunkDirtyRect>,
                             const Parent&>> all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto& w                             = sand_world.get_mut();
    auto& st                            = app_state.get_mut();

    // Only process input when the primary window has OS focus.
    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [primary_window] = *win_opt;
    if (!primary_window.focused) return;

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
        for (auto&& chunk : sim->iter_chunks_mut()) chunk.clear();

    if (need_reset) {
        for (auto&& chunk : sim->iter_chunks_mut()) chunk.clear();
        if (!st.elements.empty()) sim->apply(fs::ops::Spawn::rect({-8, -4}, {8, 4}, st.elements[0].id));
    }

    if ((lmb || rmb) && !ImGui::GetIO().WantCaptureMouse) {
        auto cam_opt = cameras.single();
        if (!cam_opt.has_value()) return;

        auto&& [cam, cam_transform] = *cam_opt;
        auto [rel_x, rel_y]         = primary_window.relative_cursor_pos();
        glm::vec2 world_cursor =
            fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_transform);

        float cs         = w.cell_size();
        std::int32_t cx_ = static_cast<std::int32_t>(std::floor(world_cursor.x / cs));
        std::int32_t cy_ = static_cast<std::int32_t>(std::floor(world_cursor.y / cs));
        std::int32_t br  = st.brush_radius;

        if (lmb && !st.elements.empty()) {
            switch (st.op_mode) {
                case OpMode::Paint:
                    sim->apply(fs::ops::Spawn::circle({cx_, cy_}, st.elements[st.selected_idx].id, br));
                    break;
                case OpMode::Erase:
                    sim->apply(fs::ops::Remove::circle({cx_, cy_}, br));
                    break;
                case OpMode::Heat:
                    sim->apply(fs::ops::Heat::circle({cx_, cy_}, st.heat_energy, br));
                    break;
                case OpMode::Cool:
                    sim->apply(fs::ops::Heat::circle({cx_, cy_}, -st.heat_energy, br));
                    break;
            }
        }
        if (rmb) sim->apply(fs::ops::Remove::circle({cx_, cy_}, br));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Explosion test: press F to detonate at cursor position.
//   blast_radius  — inner circle, cells removed.
//   push_radius   — outer circle, cells receive an outward velocity impulse
//                   scaled by intensity * (1 - dist/push_radius).
// ──────────────────────────────────────────────────────────────────────────────
void explosion_system(ResMut<SandAppState> app_state,
                      Res<input::ButtonInput<input::KeyCode>> keys,
                      Res<fs::ElementRegistry> registry,
                      Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
                      Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
                      Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras,
                      Query<Item<Mut<fs::ChunkElementGrid>,
                                 Mut<fs::ChunkAirGrid>,
                                 Mut<fs::ChunkThermalGrid>,
                                 const fs::SandChunkPos&,
                                 Mut<fs::SandChunkDirtyRect>,
                                 const Parent&>> all_chunks) {
    if (!keys->just_pressed(input::KeyCode::KeyF)) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [primary_window] = *win_opt;
    if (!primary_window.focused) return;

    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam, cam_tf] = *cam_opt;

    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *world_opt;
    auto& w                             = sand_world.get_mut();

    auto sim = make_sim(w, registry.get(), maybe_children, all_chunks);
    if (!sim.has_value()) return;

    auto [rel_x, rel_y] = primary_window.relative_cursor_pos();
    glm::vec2 world_cursor =
        fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_tf);

    float cs       = w.cell_size();
    const auto& st = app_state.get();
    auto origin_cx = static_cast<std::int32_t>(std::floor(world_cursor.x / cs));
    auto origin_cy = static_cast<std::int32_t>(std::floor(world_cursor.y / cs));

    sim->apply(fs::ops::Explode({origin_cx, origin_cy})
                   .with_intensity(st.explosion_intensity)
                   .with_blast_radius(st.explosion_blast_radius)
                   .with_push_radius(st.explosion_push_radius));
}

// ──────────────────────────────────────────────────────────────────────────────
// Debug window: LMB to add a chunk, RMB to remove a chunk at the clicked pos.
// ──────────────────────────────────────────────────────────────────────────────
void debug_window_chunk_input(Commands cmd,
                              Res<SandAppState> app_state,
                              Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                              Query<Item<const window::CachedWindow&>> all_windows,
                              Query<Item<const render::camera::Camera&, const transform::Transform&>,
                                    Filter<With<core_graph::core_2d::Camera2D, DebugCamera>>> debug_cameras,
                              Query<Item<const fs::SandWorld&>, With<fs::SimulatedByPlugin>> worlds,
                              Query<Item<Entity, const fs::SandChunkPos&, const Parent&>> chunks) {
    const auto& st   = app_state.get();
    Entity debug_win = st.debug_window_entity;

    bool lmb = mouse_buttons->pressed(input::MouseButton::MouseButtonLeft);
    bool rmb = mouse_buttons->pressed(input::MouseButton::MouseButtonRight);
    if (!lmb && !rmb) return;

    auto win_opt = all_windows.get(debug_win);
    if (!win_opt.has_value()) return;
    auto&& [win] = *win_opt;
    if (!win.focused) return;

    auto cam_opt = debug_cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam, cam_tf] = *cam_opt;

    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world] = *world_opt;
    Entity world_ent    = st.world_entity;

    auto [rel_x, rel_y] = win.relative_cursor_pos();
    glm::vec2 world_cursor =
        fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_tf);

    std::size_t shift    = sand_world.chunk_shift();
    std::int32_t chunk_w = static_cast<std::int32_t>(std::size_t(1) << shift);
    float cs             = sand_world.cell_size();
    float chunk_size     = static_cast<float>(chunk_w) * cs;

    auto floor_div   = [](float v, float d) -> std::int32_t { return static_cast<std::int32_t>(std::floor(v / d)); };
    std::int32_t tcx = floor_div(world_cursor.x, chunk_size);
    std::int32_t tcy = floor_div(world_cursor.y, chunk_size);

    if (lmb) {
        bool exists = false;
        for (auto&& [ent, pos, par] : chunks.iter()) {
            if (par.entity() != world_ent) continue;
            if (pos.value[0] == tcx && pos.value[1] == tcy) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            std::array<std::uint32_t, fs::kDim> dims;
            dims.fill(static_cast<std::uint32_t>(chunk_w));
            cmd.entity(world_ent).spawn(
                fs::ChunkElementGrid(dims), fs::ChunkAirGrid(dims, fs::AirCell{}), fs::ChunkThermalGrid(dims),
                fs::SandChunkPos{{tcx, tcy}},
                transform::Transform{.translation = glm::vec3(static_cast<float>(tcx * chunk_w) * cs,
                                                              static_cast<float>(tcy * chunk_w) * cs, 0.0f)});
        }
    }

    if (rmb) {
        for (auto&& [ent, pos, par] : chunks.iter()) {
            if (par.entity() != world_ent) continue;
            if (pos.value[0] == tcx && pos.value[1] == tcy) {
                cmd.entity(ent).despawn();
                break;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Startup: seed an initial sand pile once chunks exist.
// ──────────────────────────────────────────────────────────────────────────────
void seed(Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
          Res<fs::ElementRegistry> registry,
          Res<SandAppState> app_state,
          Query<Item<Mut<fs::ChunkElementGrid>,
                     Mut<fs::ChunkAirGrid>,
                     Mut<fs::ChunkThermalGrid>,
                     const fs::SandChunkPos&,
                     Mut<fs::SandChunkDirtyRect>,
                     const Parent&>> all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto sim                            = make_sim(sand_world.get_mut(), registry.get(), maybe_children, all_chunks);
    if (!sim.has_value()) return;
    const auto& st = app_state.get();
    if (st.elements.empty()) return;
    const auto& ei = st.elements[0];  // seed with sand
    sim->apply(fs::ops::Spawn::rect({-8, -4}, {8, 4}, ei.id));
}

// ──────────────────────────────────────────────────────────────────────────────
// freefall_overlay_system: spawns/updates/despawns semi-transparent cyan meshes
// that visualize which cells are currently in freefall (active simulation).
// Toggled by "Show Freefall" in the settings UI.
// ──────────────────────────────────────────────────────────────────────────────
void freefall_overlay_system(
    Commands cmd,
    ResMut<SandAppState> app_state,
    ResMut<assets::Assets<mesh::Mesh>> meshes,
    Query<Item<const fs::SandWorld&>, With<fs::SimulatedByPlugin>> worlds,
    Query<Item<Entity, const fs::ChunkElementGrid&, const transform::Transform&, const Parent&>> chunks,
    Query<Item<Mut<mesh::Mesh2d>>, With<FreefallOverlay>> overlay_meshes) {
    auto& st = app_state.get_mut();

    // If toggled off, despawn all overlays and clear the map.
    if (!st.show_freefall) {
        for (auto& [chunk_ent, overlay_ent] : st.freefall_overlays) cmd.entity(overlay_ent).despawn();
        st.freefall_overlays.clear();
        return;
    }

    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world] = *world_opt;
    float cell_size     = sand_world.cell_size();

    // Helper: build a mesh of cyan quads for all freefalling cells in a chunk.
    auto build_freefall_mesh = [cell_size](const fs::ChunkElementGrid& chunk) -> mesh::Mesh {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        std::uint32_t base = 0;
        for (auto&& [pos, elem] : chunk.iter()) {
            if (!elem.freefall()) continue;
            float x = static_cast<float>(pos[0]) * cell_size;
            float y = static_cast<float>(pos[1]) * cell_size;
            positions.push_back({x, y, 0.0f});
            positions.push_back({x + cell_size, y, 0.0f});
            positions.push_back({x + cell_size, y + cell_size, 0.0f});
            positions.push_back({x, y + cell_size, 0.0f});
            glm::vec4 col{0.0f, 0.9f, 1.0f, 0.45f};
            colors.push_back(col);
            colors.push_back(col);
            colors.push_back(col);
            colors.push_back(col);
            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base);
            base += 4;
        }
        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint32_t>(indices);
    };

    // Remove overlays for chunks that no longer exist.
    std::vector<Entity> to_remove;
    for (auto& [chunk_ent, overlay_ent] : st.freefall_overlays) {
        if (!chunks.get(chunk_ent).has_value()) {
            cmd.entity(overlay_ent).despawn();
            to_remove.push_back(chunk_ent);
        }
    }
    for (auto e : to_remove) st.freefall_overlays.erase(e);

    // Update or spawn overlay for each chunk.
    for (auto&& [chunk_ent, chunk, tf, parent_comp] : chunks.iter()) {
        auto new_mesh = build_freefall_mesh(chunk);
        auto handle   = meshes->emplace(std::move(new_mesh));
        if (auto it = st.freefall_overlays.find(chunk_ent); it != st.freefall_overlays.end()) {
            // Update mesh on existing overlay entity.
            if (auto opt = overlay_meshes.get(it->second)) {
                auto&& [m2d]  = *opt;
                m2d.get_mut() = mesh::Mesh2d{handle};
            }
        } else {
            // Spawn new overlay as sibling with same world position.
            auto overlay_ent =
                cmd.spawn(FreefallOverlay{}, mesh::Mesh2d{handle},
                          mesh::MeshMaterial2d{.color      = {0.0f, 0.9f, 1.0f, 0.45f},
                                               .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                          transform::Transform{.translation = tf.translation + glm::vec3{0.0f, 0.0f, 0.5f}},
                          render::camera::RenderLayer::layer(2))
                    .id();
            st.freefall_overlays[chunk_ent] = overlay_ent;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// dirty_rect_overlay_system: spawns/updates/despawns translucent red quads on
// render layer 1 that visualize each chunk's active dirty rectangle.
// Visible in the debug window (RenderLayer::all()) and optionally in the main
// window when "Show Dirty Rects" is toggled on.
// ──────────────────────────────────────────────────────────────────────────────
void dirty_rect_overlay_system(Commands cmd,
                               ResMut<SandAppState> app_state,
                               ResMut<assets::Assets<mesh::Mesh>> meshes,
                               Query<Item<const fs::SandWorld&>, With<fs::SimulatedByPlugin>> worlds,
                               ParamSet<Query<Item<Entity,
                                                   const fs::SandChunkPos&,
                                                   const fs::SandChunkDirtyRect&,
                                                   const transform::Transform&,
                                                   const Parent&>>,
                                        Query<Item<Mut<transform::Transform>>, With<DirtyRectOverlay>>> queries) {
    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world] = *world_opt;
    float cell_size     = sand_world.cell_size();

    auto& st = app_state.get_mut();

    // Collect active dirty-rect chunks and their world data.
    struct ActiveRect {
        glm::vec3 chunk_origin;
        fs::SandChunkDirtyRect dirty_rect;
    };
    std::unordered_map<Entity, ActiveRect> active;
    {
        auto&& [chunks, overlay_transforms] = queries.get();
        for (auto&& [ent, pos, dirty_rect, tf, parent] : chunks.iter()) {
            if (!dirty_rect.active()) continue;
            active[ent] = {tf.translation, dirty_rect};
        }
    }

    // Remove overlays whose chunk is no longer in the active set.
    std::vector<Entity> to_remove;
    for (auto& [chunk_ent, overlay_ent] : st.dirty_rect_overlays) {
        if (active.find(chunk_ent) == active.end()) {
            cmd.entity(overlay_ent).despawn();
            to_remove.push_back(chunk_ent);
        }
    }
    for (auto e : to_remove) st.dirty_rect_overlays.erase(e);

    // Create or update overlays for active chunks.
    auto&& [chunks, overlay_transforms] = queries.get();
    for (auto& [chunk_ent, ar] : active) {
        float x0       = ar.chunk_origin.x + static_cast<float>(ar.dirty_rect.get_xmin()) * cell_size;
        float x1       = ar.chunk_origin.x + static_cast<float>(ar.dirty_rect.get_xmax() + 1) * cell_size;
        float y0       = ar.chunk_origin.y + static_cast<float>(ar.dirty_rect.get_ymin()) * cell_size;
        float y1       = ar.chunk_origin.y + static_cast<float>(ar.dirty_rect.get_ymax() + 1) * cell_size;
        float center_x = (x0 + x1) * 0.5f;
        float center_y = (y0 + y1) * 0.5f;
        float width    = x1 - x0;
        float height   = y1 - y0;

        if (auto it = st.dirty_rect_overlays.find(chunk_ent); it != st.dirty_rect_overlays.end()) {
            // Update existing overlay transform in place.
            if (auto opt = overlay_transforms.get(it->second)) {
                auto&& [tf]              = *opt;
                tf.get_mut().translation = {center_x, center_y, 1.0f};
                tf.get_mut().scaler      = {width, height, 1.0f};
            }
        } else {
            // Spawn a new overlay entity (unit quad scaled to dirty-rect size).
            auto handle      = meshes->emplace(mesh::make_box2d(1.0f, 1.0f));
            auto overlay_ent = cmd.spawn(DirtyRectOverlay{}, mesh::Mesh2d{handle},
                                         mesh::MeshMaterial2d{.color      = {1.0f, 0.3f, 0.1f, 0.35f},
                                                              .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                                         transform::Transform{.translation = {center_x, center_y, 1.0f},
                                                              .scaler      = {width, height, 1.0f}},
                                         render::camera::RenderLayer::layer(1))
                                   .id();
            st.dirty_rect_overlays[chunk_ent] = overlay_ent;
        }
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

    constexpr std::uint8_t kTagCondensable = 1;

    // ── Sand (Powder) ─────────────────────────────────────────────────────────
    reg(
        fs::ElementBase{
            .name                 = "sand",
            .density              = 1.5f,
            .type                 = fs::ElementType::Powder,
            .specific_heat        = 800.0f,
            .thermal_conductivity = 0.25f,
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd += 0x9e3779b97f4a7c15ULL;
                    sd = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    sd = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    sd ^= (sd >> 31);
                    float t = static_cast<float>(sd) / static_cast<float>(std::numeric_limits<std::uint64_t>::max());
                    return fs::Element{id, glm::vec4(0.80f + t * 0.18f, 0.68f + t * 0.15f, 0.18f + t * 0.08f, 1.0f)};
                },
        },
        glm::vec4(0.80f, 0.68f, 0.18f, 1.0f));

    // ── Water (Liquid) ────────────────────────────────────────────────────────
    // Boils at 100 °C → steam; spawned steam is tagged condensable.
    reg(
        fs::ElementBase{
            .name                 = "water",
            .density              = 1.0f,
            .type                 = fs::ElementType::Liquid,
            .restitution          = 0.05f,
            .friction             = 0.1f,
            .awake_rate           = 1.0f,
            .specific_heat        = 4184.0f,
            .thermal_conductivity = 0.6f,
            .actions =
                {
                    fs::ElementAction{
                        .conditions = {fs::TemperatureBelow{273.0f, true}, fs::StagingHeat{334000.0f}},
                        .action     = fs::TransformTo{"ice", 0},
                    },
                    fs::ElementAction{
                        .conditions = {fs::TemperatureAbove{373.0f, true}, fs::StagingHeat{2260000.0f}},
                        .action     = fs::TransformTo{"steam", 0},
                    },
                    fs::ElementAction{
                        .conditions = {fs::TemperatureAbove{373.0f}, fs::RandomTick{0.3f, {}, 1}},
                        .action     = fs::SpawnNearby{"steam", 0, 1, 3, kTagCondensable},
                    },
                },
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd += 0x6c62272e07bb0142ULL;
                    sd = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    sd ^= (sd >> 31);
                    float t = static_cast<float>(sd & 0xFFFF) / 65535.0f;
                    return fs::Element{id, glm::vec4(0.15f + t * 0.15f, 0.45f + t * 0.15f, 0.85f + t * 0.12f, 1.0f)};
                },
        },
        glm::vec4(0.20f, 0.50f, 0.90f, 1.0f));

    // ── Stone (Solid) ─────────────────────────────────────────────────────────
    reg(
        fs::ElementBase{
            .name                 = "stone",
            .density              = 5.0f,
            .type                 = fs::ElementType::Solid,
            .specific_heat        = 800.0f,
            .thermal_conductivity = 2.0f,
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float v = 0.40f + t * 0.20f;
                    return fs::Element{id, glm::vec4(v, v, v, 1.0f)};
                },
        },
        glm::vec4(0.50f, 0.50f, 0.50f, 1.0f));

    // ── Smoke (Gas) ───────────────────────────────────────────────────────────
    // Dissipates via random tick.
    reg(
        fs::ElementBase{
            .name                 = "smoke",
            .density              = 0.001f,
            .type                 = fs::ElementType::Gas,
            .awake_rate           = 1.0f,
            .specific_heat        = 1000.0f,
            .thermal_conductivity = 0.02f,
            .actions =
                {
                    fs::ElementAction{
                        .conditions = {fs::RandomTick{0.15f, {}, 2}},
                        .action     = fs::Despawn{},
                    },
                },
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float v = 0.55f + t * 0.25f;
                    return fs::Element{id, glm::vec4(v, v, v, 0.8f)};
                },
        },
        glm::vec4(0.70f, 0.70f, 0.70f, 0.8f));

    // ── Wood (Solid) ──────────────────────────────────────────────────────────
    // Burns at 500K, smokes while burning, despawns at high temperature.
    reg(
        fs::ElementBase{
            .name                 = "wood",
            .density              = 0.8f,
            .type                 = fs::ElementType::Solid,
            .specific_heat        = 2000.0f,
            .thermal_conductivity = 0.15f,
            .ignition_temperature = 500.0f,
            .heat_of_combustion   = 17000000.0f,  // 17 MJ/kg, dry wood
            .actions =
                {
                    fs::ElementAction{
                        .conditions = {fs::TemperatureAbove{500.0f}, fs::RandomTick{0.3f, {}, 3}},
                        .action     = fs::Ignite{},
                    },
                    fs::ElementAction{
                        .conditions = {fs::TemperatureBelow{500.0f}, fs::IsBurning{}, fs::RandomTick{0.2f, {}, 3}},
                        .action     = fs::Extinguish{},
                    },
                    fs::ElementAction{
                        .conditions = {fs::IsBurning{}, fs::RandomTick{0.4f, {}, 1}},
                        .action     = fs::SpawnNearby{"smoke", 0, 1, 3},
                    },
                },
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    return fs::Element{id, glm::vec4(0.55f + t * 0.20f, 0.30f + t * 0.15f, 0.10f + t * 0.10f, 1.0f)};
                },
        },
        glm::vec4(0.60f, 0.35f, 0.15f, 1.0f));

    // ── Steam (Gas) ───────────────────────────────────────────────────────────
    // Condensable steam (from boiling water) can return to water; all steam
    // eventually despawns.
    reg(
        fs::ElementBase{
            .name                 = "steam",
            .density              = 0.0006f,
            .type                 = fs::ElementType::Gas,
            .awake_rate           = 1.0f,
            .specific_heat        = 2000.0f,
            .thermal_conductivity = 0.02f,
            .actions =
                {
                    fs::ElementAction{
                        .conditions = {fs::HasTag{kTagCondensable}, fs::RandomTick{0.1f, {}, 2}},
                        .action     = fs::TransformTo{"water", 0},
                    },
                    fs::ElementAction{
                        .conditions = {fs::RandomTick{0.08f, {}, 2}},
                        .action     = fs::Despawn{},
                    },
                },
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float v = 0.85f + t * 0.12f;
                    return fs::Element{id, glm::vec4(v, v, v, 0.55f)};
                },
        },
        glm::vec4(0.90f, 0.90f, 0.95f, 0.55f));

    // ── Lava (Liquid) ──────────────────────────────────────────────────────────
    // Starts hot, cools to stone below ~900 K.
    reg(fs::ElementBase{
            .name                 = "lava",
            .density              = 3.0f,
            .type                 = fs::ElementType::Liquid,
            .restitution          = 0.1f,
            .friction             = 0.3f,
            .awake_rate           = 1.0f,
            .specific_heat        = 1130.0f,
            .thermal_conductivity = 1.5f,
            .actions =
                {
                    fs::ElementAction{
                        .conditions = {fs::TemperatureBelow{900.0f, true}, fs::StagingHeat{500000.0f}},
                        .action     = fs::TransformTo{"stone", 0},
                    },
                },
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float r = 0.85f + t * 0.15f;
                    float g = 0.25f + t * 0.25f;
                    float b = t * 0.12f;
                    return fs::Element{id, glm::vec4(r, g, b, 1.0f)};
                },
            .thermal_construct_func = [](std::size_t, const fs::ElementBase&,
                                         std::uint64_t) { return fs::ThermalCell{1500.0f}; },
        },
        glm::vec4(0.95f, 0.40f, 0.05f, 1.0f));

    // ── Ice (Solid) ────────────────────────────────────────────────────────────
    // Melts to water above 0 °C.
    reg(fs::ElementBase{
            .name                 = "ice",
            .density              = 0.92f,
            .type                 = fs::ElementType::Solid,
            .restitution          = 0.05f,
            .friction             = 0.1f,
            .specific_heat        = 2100.0f,
            .thermal_conductivity = 2.2f,
            .actions =
                {
                    fs::ElementAction{
                        .conditions = {fs::TemperatureAbove{273.0f, true}, fs::StagingHeat{334000.0f}},
                        .action     = fs::TransformTo{"water", 0},
                    },
                },
            .construct_func =
                [](std::size_t id, const fs::ElementBase&, std::uint64_t sd) {
                    sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    float t = static_cast<float>(sd & 0xFF) / 255.0f;
                    float v = 0.80f + t * 0.18f;
                    return fs::Element{id, glm::vec4(v, v, 1.0f, 0.85f)};
                },
            .thermal_construct_func = [](std::size_t, const fs::ElementBase&,
                                         std::uint64_t) { return fs::ThermalCell{250.0f}; },
        },
        glm::vec4(0.75f, 0.85f, 1.0f, 0.85f));

    // ── Resolve cross-reference element names in transitions ────────────────
    auto resolve_res = registry.resolve_all_references();
    if (!resolve_res.has_value()) {
        return;  // Failed to resolve element transition references
    }

    cmd.insert_resource(std::move(registry));

    // ── Debug window (second OS window, shows all render layers) ─────────────
    window::Window debug_win;
    debug_win.title        = "Sand Debug View  |  dirty rects = layer 1";
    debug_win.size         = {640, 480};
    auto debug_win_ent     = cmd.spawn(std::move(debug_win)).id();
    st.debug_window_entity = debug_win_ent;

    // ── Main camera: renders layers 0+ except layer 1 (dirty rects hidden) ───
    {
        core_graph::core_2d::Camera2DBundle bundle{};
        // scale = 1/64 so that 1/16 m cells render at ~4 px each (64 px/world-unit)
        bundle.projection   = render::camera::Projection(render::camera::OrthographicProjection{.scale = 1.0f / 64.0f});
        bundle.render_layer = render::camera::RenderLayer::all_except(std::array{std::size_t{1}});
        cmd.spawn(std::move(bundle)).insert(MainCamera{});
    }

    // ── Debug camera: layer 2 (outlines) + layer 1 (dirty rects), NOT sand (layer 0)
    {
        core_graph::core_2d::Camera2DBundle bundle{};
        bundle.camera.render_target = render::camera::RenderTarget::from_window(debug_win_ent);
        bundle.camera.order         = 1;
        bundle.render_layer         = render::camera::RenderLayer::layers(std::array{std::size_t{1}, std::size_t{2}});
        cmd.spawn(std::move(bundle)).insert(DebugCamera{});
    }

    constexpr std::size_t chunk_shift = 5;
    constexpr float cell_size         = 1.0f / 16.0f;  // 1/16 m per cell, 1 unit = 1 m
    constexpr std::int32_t chunk_w    = static_cast<std::int32_t>(std::size_t(1) << chunk_shift);
    constexpr std::int32_t chunks_x   = 4;
    constexpr std::int32_t chunks_y   = 3;

    fs::SandWorld sand_world(chunk_shift, cell_size);
    sand_world.set_missing_chunk_as_solid(true);
    auto world_cmds = cmd.spawn(std::move(sand_world), transform::Transform{}, fs::SimulatedByPlugin{},
                                fs::MeshBuildByPlugin{}, fs::SandWorldDebug{});
    st.world_entity = world_cmds.id();

    for (std::int32_t cy = -chunks_y; cy < chunks_y; ++cy) {
        for (std::int32_t cx = -chunks_x; cx < chunks_x; ++cx) {
            std::array<std::uint32_t, fs::kDim> dims;
            dims.fill(static_cast<std::uint32_t>(chunk_w));
            world_cmds.spawn(
                fs::ChunkElementGrid(dims), fs::ChunkAirGrid(dims, fs::AirCell{}), fs::ChunkThermalGrid(dims),
                fs::SandChunkPos{{cx, cy}},
                transform::Transform{.translation = glm::vec3(static_cast<float>(cx * chunk_w) * cell_size,
                                                              static_cast<float>(cy * chunk_w) * cell_size, 0.0f)});
        }
    }

    cmd.insert_resource(std::move(st));
}

// ──────────────────────────────────────────────────────────────────────────────
// Element hover info — show element data when hovering over a cell.
// ──────────────────────────────────────────────────────────────────────────────
void element_hover_info(
    Res<fs::ElementRegistry> registry,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
    Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras,
    Query<Item<Mut<fs::ChunkElementGrid>,
               Mut<fs::ChunkAirGrid>,
               Mut<fs::ChunkThermalGrid>,
               const fs::SandChunkPos&,
               Mut<fs::SandChunkDirtyRect>,
               const Parent&>> all_chunks) {
    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *world_opt;
    auto& w                             = sand_world.get_mut();

    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [primary_window] = *win_opt;

    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam, cam_transform] = *cam_opt;

    auto [rel_x, rel_y] = primary_window.relative_cursor_pos();
    glm::vec2 world_cursor =
        fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_transform);

    float cs         = w.cell_size();
    std::int32_t cx_ = static_cast<std::int32_t>(std::floor(world_cursor.x / cs));
    std::int32_t cy_ = static_cast<std::int32_t>(std::floor(world_cursor.y / cs));

    auto sim = make_sim(w, registry.get(), maybe_children, all_chunks);
    if (!sim.has_value()) return;

    auto cell = sim->get_cell<fs::Element>({cx_, cy_});

    ImGui::Begin("Cell Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Position: (%d, %d)", cx_, cy_);

    if (cell.has_value()) {
        const fs::ElementBase& base = registry.get()[cell->get().base_id];
        const auto& elem            = cell->get();
        auto therm_cell             = sim->get_cell<fs::ThermalCell>({cx_, cy_});
        float temp                  = therm_cell.has_value() ? therm_cell->get().temperature : 293.0f;
        float staging               = therm_cell.has_value() ? therm_cell->get().staging_heat : 0.0f;

        ImGui::Text("Element: %s", base.name.c_str());
        ImGui::Text("Type: %s", [](fs::ElementType t) {
            switch (t) {
                case fs::ElementType::Solid:
                    return "Solid";
                case fs::ElementType::Powder:
                    return "Powder";
                case fs::ElementType::Liquid:
                    return "Liquid";
                case fs::ElementType::Gas:
                    return "Gas";
                case fs::ElementType::Body:
                    return "Body";
                default:
                    return "?";
            }
        }(base.type));
        ImGui::Text("Temperature: %.1f K (%.1f °C)", temp, temp - 273.15f);
        if (std::abs(staging) > 1.0f) ImGui::Text("Staging heat: %.0f J", staging);
        ImGui::Text("Density: %.2f kg/m³", base.density);
        ImGui::Text("Specific heat: %.0f J/(kg·K)", base.specific_heat);
        ImGui::Text("Thermal cond: %.2f W/(m·K)", base.thermal_conductivity);
        if (base.ignition_temperature > 0.0f) ImGui::Text("Ignition: %.0f K", base.ignition_temperature);
        if (elem.burning()) ImGui::TextColored({1.0f, 0.5f, 0.1f, 1.0f}, "BURNING");
        ImGui::Separator();
        if (!base.actions.empty()) {
            ImGui::Text("Actions:");
            for (const auto& act : base.actions) {
                std::string desc;
                std::visit(
                    [&](const auto& a) {
                        using T = std::decay_t<decltype(a)>;
                        if constexpr (std::is_same_v<T, fs::Ignite>)
                            desc = "Ignite";
                        else if constexpr (std::is_same_v<T, fs::Extinguish>)
                            desc = "Extinguish";
                        else if constexpr (std::is_same_v<T, fs::Despawn>)
                            desc = "Despawn";
                        else if constexpr (std::is_same_v<T, fs::TransformTo>)
                            desc = "→ " + a.target_name;
                        else if constexpr (std::is_same_v<T, fs::SpawnNearby>)
                            desc = "Spawn " + a.element_name;
                    },
                    act.action);
                for (const auto& c : act.conditions) {
                    std::visit(
                        [&](const auto& cv) {
                            using T = std::decay_t<decltype(cv)>;
                            if constexpr (std::is_same_v<T, fs::TemperatureAbove>)
                                desc += " [T≥" + std::to_string((int)cv.target) + "K]";
                            else if constexpr (std::is_same_v<T, fs::TemperatureBelow>)
                                desc += " [T≤" + std::to_string((int)cv.target) + "K]";
                            else if constexpr (std::is_same_v<T, fs::RandomTick>)
                                desc += " [RT]";
                            else if constexpr (std::is_same_v<T, fs::IsBurning>)
                                desc += " [Burning]";
                            else if constexpr (std::is_same_v<T, fs::HasTag>)
                                desc += " [Tag:" + std::to_string(cv.tag) + "]";
                            else if constexpr (std::is_same_v<T, fs::StagingHeat>)
                                desc += " [Staging:" + std::to_string((int)(cv.latent_heat_j_per_kg / 1000)) + "kJ/kg]";
                        },
                        c);
                }
                ImGui::Text("  %s", desc.c_str());
            }
        }
    } else {
        // No element — show air info
        auto air = sim->get_cell<fs::AirCell>({cx_, cy_});
        if (air.has_value()) {
            const auto& a = air->get();
            ImGui::Text("Air");
            ImGui::Text("Temperature: %.1f K (%.1f °C)", a.temperature, a.temperature - 273.15f);
            ImGui::Text("Density: %.4f kg/m³", a.density);
            ImGui::Text("Velocity: (%.2f, %.2f)", a.velocity.x, a.velocity.y);
        } else {
            ImGui::Text("(out of bounds)");
        }
    }
    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────────────

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Falling Sand  |  LMB paint/op  RMB erase  Tab cycle  Space pause";
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
        .add_plugins(imgui::ImGuiPlugin{
            .enable_docking   = true,
            .enable_viewports = true,
        })
        .add_plugins(time::TimePlugin{})
        .add_plugins(fs::FallingSandPlugin{})
        .add_systems(PreStartup, into(setup).set_name("fallingsand example setup"))
        .add_systems(Startup, into(seed).set_name("fallingsand example seed"))
        .add_systems(PreUpdate, into(settings_ui).after(imgui::BeginFrameSet))
        .add_systems(Update, into(input_system))
        .add_systems(Update, into(camera_control))
        .add_systems(Update, into(debug_camera_control))
        .add_systems(Update, into(explosion_system))
        .add_systems(Update, into(debug_window_chunk_input))
        .add_systems(Update, into(element_hover_info).after(imgui::BeginFrameSet))
        .add_systems(time::FixedPostUpdate, into(freefall_overlay_system))
        .add_systems(time::FixedPostUpdate, into(dirty_rect_overlay_system));

    app.run();
}
