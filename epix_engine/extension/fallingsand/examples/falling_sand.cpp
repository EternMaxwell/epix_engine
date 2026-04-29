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
struct SandAppState {
    std::vector<ElementInfo> elements;
    std::size_t selected_idx  = 0;
    std::int32_t brush_radius = 2;
    Entity world_entity       = {};
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
        all_chunks,
    Query<Item<Mut<render::camera::RenderLayer>>, Filter<With<core_graph::core_2d::Camera2D, MainCamera>>>
        main_cameras) {
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
    if (ImGui::Button("Clear (C)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks))
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset (R)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks)) {
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
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
            ortho->scale = std::clamp(ortho->scale * factor, 0.05f, 50.0f);
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
        ortho->scale = std::clamp(ortho->scale * factor, 0.05f, 50.0f);
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
void input_system(
    ResMut<SandAppState> app_state,
    Res<fs::ElementRegistry> registry,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Res<input::ButtonInput<input::KeyCode>> keys,
    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
    Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>
        all_chunks) {
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
        for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();

    if (need_reset) {
        for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
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

        if (lmb && !st.elements.empty())
            sim->apply(fs::ops::Spawn::circle({cx_, cy_}, st.elements[st.selected_idx].id, br));
        if (rmb) sim->apply(fs::ops::Remove::circle({cx_, cy_}, br));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Explosion test: press F to detonate at cursor position.
//   blast_radius  — inner circle, cells removed.
//   push_radius   — outer circle, cells receive an outward velocity impulse
//                   scaled by intensity * (1 - dist/push_radius).
// ──────────────────────────────────────────────────────────────────────────────
void explosion_system(
    ResMut<SandAppState> app_state,
    Res<input::ButtonInput<input::KeyCode>> keys,
    Res<fs::ElementRegistry> registry,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
    Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>
        all_chunks) {
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
            ext::grid::Chunk<fs::kDim> chunk(shift);
            (void)chunk.add_layer(std::make_unique<ext::grid::layers::TreeLayer<fs::kDim, fs::Element>>(shift));
            cmd.entity(world_ent).spawn(
                std::move(chunk), fs::SandChunkPos{{tcx, tcy}},
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
    Query<Item<Entity, const ext::grid::Chunk<fs::kDim>&, const transform::Transform&, const Parent&>> chunks,
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
    auto build_freefall_mesh = [cell_size](const ext::grid::Chunk<fs::kDim>& chunk) -> mesh::Mesh {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        std::uint32_t base = 0;
        for (auto&& [pos, elem] : chunk.iter<fs::Element>()) {
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
        float x0       = ar.chunk_origin.x + static_cast<float>(ar.dirty_rect.xmin) * cell_size;
        float x1       = ar.chunk_origin.x + static_cast<float>(ar.dirty_rect.xmax + 1) * cell_size;
        float y0       = ar.chunk_origin.y + static_cast<float>(ar.dirty_rect.ymin) * cell_size;
        float y1       = ar.chunk_origin.y + static_cast<float>(ar.dirty_rect.ymax + 1) * cell_size;
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

    // ── Debug window (second OS window, shows all render layers) ─────────────
    window::Window debug_win;
    debug_win.title        = "Sand Debug View  |  dirty rects = layer 1";
    debug_win.size         = {640, 480};
    auto debug_win_ent     = cmd.spawn(std::move(debug_win)).id();
    st.debug_window_entity = debug_win_ent;

    // ── Main camera: renders layers 0+ except layer 1 (dirty rects hidden) ───
    {
        core_graph::core_2d::Camera2DBundle bundle{};
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
    constexpr float cell_size         = 4.0f;
    constexpr std::int32_t chunk_w    = static_cast<std::int32_t>(std::size_t(1) << chunk_shift);
    constexpr std::int32_t chunks_x   = 4;
    constexpr std::int32_t chunks_y   = 3;

    fs::SandWorld sand_world(chunk_shift, cell_size);
    sand_world.set_missing_chunk_as_solid(true);
    auto world_cmds =
        cmd.spawn(std::move(sand_world), transform::Transform{}, fs::SimulatedByPlugin{}, fs::MeshBuildByPlugin{});
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
        .add_systems(Update, into(input_system))
        .add_systems(Update, into(camera_control))
        .add_systems(Update, into(debug_camera_control))
        .add_systems(Update, into(explosion_system))
        .add_systems(Update, into(debug_window_chunk_input))
        .add_systems(time::FixedPostUpdate, into(freefall_overlay_system))
        .add_systems(time::FixedPostUpdate, into(dirty_rect_overlay_system));

    app.run();
}
