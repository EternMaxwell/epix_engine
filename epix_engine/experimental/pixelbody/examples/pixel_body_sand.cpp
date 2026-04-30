// Pixel body + falling-sand demo.

#include <box2d/box2d.h>
#include <box2d/collision.h>
#include <box2d/id.h>
#include <box2d/types.h>
#include <imgui.h>
#ifndef EPIX_IMPORT_STD
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
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
import epix.experimental.pixelbody;
import epix.time;

using namespace epix;
using namespace epix::core;
namespace fs = epix::ext::fallingsand;
namespace pb = epix::experimental::pixelbody;

struct MainCamera {};

/** Overlay markers. */
struct DirtyRectOverlay {};
struct FreefallOverlay {};
struct ChunkChainOverlay {};
struct BodyOutlineOverlay {};

struct AppState {
    Entity world_entity            = {};
    std::size_t stone_id           = 0;
    std::size_t sand_id            = 0;
    std::size_t water_id           = 0;
    int paint_mode                 = 0;  // 0 = sand, 1 = water, 2 = stone, 3 = erase
    int paint_radius               = 4;
    bool cam_dragging              = false;
    glm::vec2 cam_drag_start_mouse = {};
    glm::vec3 cam_drag_start_pos   = {};
    // Debug overlay toggles + bookkeeping.
    bool show_dirty_rects   = false;
    bool show_freefall      = false;
    bool show_chunk_chains  = false;
    bool show_body_outlines = false;
    std::unordered_map<Entity, Entity> dirty_rect_overlays;
    std::unordered_map<Entity, Entity> freefall_overlays;
    std::unordered_map<Entity, Entity> chunk_chain_overlays;
    std::unordered_map<Entity, Entity> body_outline_overlays;
};

// ──────────────────────────────────────────────────────────────────────────────
// Setup: register elements, spawn camera, sand world (with stone floor) and
// pixel-body world.
// ──────────────────────────────────────────────────────────────────────────────
void setup(Commands cmd) {
    fs::ElementRegistry registry;
    AppState st;

    auto reg_sand = registry.register_element(fs::ElementBase{
        .name    = "sand",
        .density = 1.5f,
        .type    = fs::ElementType::Powder,
        .color_func =
            [](std::uint64_t sd) {
                sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                float t = static_cast<float>(sd & 0xFFFF) / 65535.0f;
                return glm::vec4(0.85f + t * 0.10f, 0.70f + t * 0.10f, 0.20f + t * 0.05f, 1.0f);
            },
    });
    if (reg_sand.has_value()) st.sand_id = *reg_sand;

    auto reg_stone = registry.register_element(fs::ElementBase{
        .name        = "stone",
        .density     = 5.0f,
        .type        = fs::ElementType::Solid,
        .restitution = 0.05f,
        .friction    = 0.6f,
        .color_func =
            [](std::uint64_t sd) {
                sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                float t = static_cast<float>(sd & 0xFF) / 255.0f;
                float v = 0.40f + t * 0.20f;
                return glm::vec4(v, v, v, 1.0f);
            },
    });
    if (reg_stone.has_value()) st.stone_id = *reg_stone;

    auto reg_water = registry.register_element(fs::ElementBase{
        .name    = "water",
        .density = 1.0f,
        .type    = fs::ElementType::Liquid,
        .color_func =
            [](std::uint64_t sd) {
                sd      = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                float t = static_cast<float>(sd & 0xFF) / 255.0f;
                return glm::vec4(0.20f + t * 0.05f, 0.40f + t * 0.10f, 0.85f + t * 0.10f, 0.85f);
            },
    });
    if (reg_water.has_value()) st.water_id = *reg_water;

    cmd.insert_resource(std::move(registry));

    // Main camera.
    {
        core_graph::core_2d::Camera2DBundle bundle{};
        cmd.spawn(std::move(bundle)).insert(MainCamera{});
    }

    // Sand world.
    constexpr std::size_t chunk_shift = 5;
    constexpr float cell_size         = 4.0f;
    constexpr std::int32_t chunk_w    = static_cast<std::int32_t>(std::size_t(1) << chunk_shift);
    constexpr std::int32_t chunks_x   = 4;
    constexpr std::int32_t chunks_y   = 3;

    fs::SandWorld sand_world(chunk_shift, cell_size);
    pb::PixelBodyWorld pbw{};
    pbw.cell_size     = cell_size;
    pbw.gravity_cells = glm::vec2(0.0f, -300.0f);
    auto world_cmds = cmd.spawn(std::move(sand_world), std::move(pbw), transform::Transform{}, fs::SimulatedByPlugin{},
                                fs::MeshBuildByPlugin{});
    st.world_entity = world_cmds.id();
    for (std::int32_t cy = -chunks_y; cy < chunks_y; ++cy) {
        for (std::int32_t cx = -chunks_x; cx < chunks_x; ++cx) {
            ext::grid::Chunk<fs::kDim> chunk(chunk_shift);
            (void)chunk.add_layer(std::make_unique<ext::grid::layers::TreeLayer<fs::kDim, fs::Element>>(chunk_shift));
            world_cmds.spawn(std::move(chunk), fs::SandChunkPos{{cx, cy}},
                             transform::Transform{
                                 .translation = glm::vec3(static_cast<float>(cx * chunk_w) * cell_size,
                                                          static_cast<float>(cy * chunk_w) * cell_size, 0.0f),
                             });
        }
    }

    cmd.insert_resource(std::move(st));
}

// ──────────────────────────────────────────────────────────────────────────────
// Startup: paint a stone floor in the sand world and spawn one initial body.
// ──────────────────────────────────────────────────────────────────────────────
void seed(
    Res<AppState> app_state,
    Res<fs::ElementRegistry> registry,
    Commands cmd,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>, const Parent&>>
        all_chunks) {
    const auto& st = app_state.get();
    auto opt       = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    if (!maybe_children.has_value()) return;

    // Build a SandSimulation from the parent's chunks (matches falling_sand pattern).
    const auto& children = maybe_children->get().entities();
    auto chunk_range =
        children | std::views::filter([&](Entity e) { return all_chunks.get(e).has_value(); }) |
        std::views::transform(
            [&](Entity e) -> std::tuple<ext::grid::Chunk<fs::kDim>&, const fs::SandChunkPos&, fs::SandChunkDirtyRect&> {
                auto opt              = all_chunks.get(e);
                auto&& [c, p, d, par] = *opt;
                return {c.get_mut(), p, d.get_mut()};
            });
    auto sim_res = fs::SandSimulation::create(sand_world.get_mut(), registry.get(), chunk_range);
    if (!sim_res.has_value()) return;
    auto& sim = *sim_res;

    // Paint a stone floor (long horizontal slab) and a couple of stone pillars.
    sim.apply(fs::ops::Spawn::rect({-120, -90}, {120, -75}, st.stone_id));
    sim.apply(fs::ops::Spawn::rect({-40, -75}, {-30, -50}, st.stone_id));
    sim.apply(fs::ops::Spawn::rect({30, -75}, {40, -55}, st.stone_id));

    // Spawn a starter pixel body high above.
    auto body = pb::PixelBody::make_solid(20, 20, st.stone_id, registry.get(), 1);
    cmd.entity(st.world_entity)
        .spawn(std::move(body), transform::Transform{.translation = glm::vec3(-30.0f, 200.0f, 0.0f)}, pb::Velocity{});
}

// ──────────────────────────────────────────────────────────────────────────────
// Camera controls (scroll-zoom + MMB pan).
// ──────────────────────────────────────────────────────────────────────────────
void camera_control(ResMut<AppState> app_state,
                    EventReader<input::MouseScroll> scroll_events,
                    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                    Query<Item<Entity, const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
                    Query<Item<Mut<transform::Transform>, Mut<render::camera::Projection>>,
                          Filter<With<core_graph::core_2d::Camera2D, MainCamera>>> cameras) {
    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [win_ent, win] = *win_opt;
    auto cam_opt          = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam_tf, cam_proj] = *cam_opt;
    auto& tf                  = cam_tf.get_mut();
    auto& proj                = cam_proj.get_mut();
    auto& st                  = app_state.get_mut();

    auto ortho_opt = proj.as_orthographic();
    if (!ortho_opt.has_value()) return;
    auto* ortho = *ortho_opt;

    if (!ImGui::GetIO().WantCaptureMouse) {
        for (auto&& ev : scroll_events.read()) {
            if (ev.window != win_ent) continue;
            float factor = std::pow(1.15f, -static_cast<float>(ev.yoffset));
            ortho->scale = std::clamp(ortho->scale * factor, 0.05f, 50.0f);
        }
    }
    if (!win.focused) return;

    if (!ImGui::GetIO().WantCaptureMouse) {
        auto [cx, cy]   = win.cursor_pos;
        glm::vec2 mouse = {static_cast<float>(cx), static_cast<float>(cy)};
        if (mouse_buttons->just_pressed(input::MouseButton::MouseButtonMiddle)) {
            st.cam_dragging         = true;
            st.cam_drag_start_mouse = mouse;
            st.cam_drag_start_pos   = tf.translation;
        }
        if (st.cam_dragging) {
            if (mouse_buttons->pressed(input::MouseButton::MouseButtonMiddle)) {
                glm::vec2 delta  = mouse - st.cam_drag_start_mouse;
                float s          = ortho->scale;
                tf.translation.x = st.cam_drag_start_pos.x - delta.x * s;
                tf.translation.y = st.cam_drag_start_pos.y + delta.y * s;
            } else {
                st.cam_dragging = false;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Click-to-spawn pixel body at cursor (RMB).
// ──────────────────────────────────────────────────────────────────────────────
void spawn_body_on_click(
    Commands cmd,
    Res<AppState> app_state,
    Res<fs::ElementRegistry> registry,
    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
    Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (!mouse_buttons->just_pressed(input::MouseButton::MouseButtonRight)) return;

    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [win] = *win_opt;
    if (!win.focused) return;

    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam, cam_tf] = *cam_opt;

    auto [rel_x, rel_y] = win.relative_cursor_pos();
    glm::vec2 world_cursor =
        fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_tf);

    const auto& st = app_state.get();
    auto body      = pb::PixelBody::make_solid(16, 16, st.stone_id, registry.get(), 7);
    cmd.entity(st.world_entity)
        .spawn(std::move(body), transform::Transform{.translation = glm::vec3(world_cursor.x, world_cursor.y, 0.0f)},
               pb::Velocity{});
}

// ──────────────────────────────────────────────────────────────────────────────
// Paint sand/water/stone/erase under the cursor (LMB held).
// ──────────────────────────────────────────────────────────────────────────────
void paint_sand_on_drag(
    Res<AppState> app_state,
    Res<fs::ElementRegistry> registry,
    Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
    Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
    Query<Item<const render::camera::Camera&, const transform::Transform&>, With<MainCamera>> cameras,
    Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>>> chunks_q) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (!mouse_buttons->pressed(input::MouseButton::MouseButtonLeft)) return;

    auto win_opt = windows.single();
    if (!win_opt.has_value()) return;
    auto&& [win] = *win_opt;
    if (!win.focused) return;
    auto cam_opt = cameras.single();
    if (!cam_opt.has_value()) return;
    auto&& [cam, cam_tf] = *cam_opt;

    auto world_opt = worlds.get(app_state->world_entity);
    if (!world_opt.has_value()) return;
    auto&& [sw_mut, children_opt] = *world_opt;
    if (!children_opt.has_value()) return;
    auto& sw  = sw_mut.get_mut();
    float scs = sw.cell_size();

    auto [rel_x, rel_y] = win.relative_cursor_pos();
    glm::vec2 world_cursor =
        fs::relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)), cam, cam_tf);

    auto chunk_range =
        children_opt->get().entities() | std::views::filter([&](Entity e) { return chunks_q.get(e).has_value(); }) |
        std::views::transform(
            [&](Entity e) -> std::tuple<ext::grid::Chunk<fs::kDim>&, const fs::SandChunkPos&, fs::SandChunkDirtyRect&> {
                auto opt         = chunks_q.get(e);
                auto&& [c, p, d] = *opt;
                return {c.get_mut(), p, d.get_mut()};
            });
    auto sim_res = fs::SandSimulation::create(sw, registry.get(), chunk_range);
    if (!sim_res.has_value()) return;
    auto& sim = *sim_res;

    const auto& st  = *app_state;
    std::int64_t cx = static_cast<std::int64_t>(std::floor(world_cursor.x / scs));
    std::int64_t cy = static_cast<std::int64_t>(std::floor(world_cursor.y / scs));
    int r           = std::max(1, st.paint_radius);

    std::size_t paint_id = st.sand_id;
    if (st.paint_mode == 1)
        paint_id = st.water_id;
    else if (st.paint_mode == 2)
        paint_id = st.stone_id;

    std::uint64_t seed_base =
        static_cast<std::uint64_t>(cx) * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(cy) * 0xBF58476D1CE4E5B9ULL;

    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            std::int64_t x = cx + dx;
            std::int64_t y = cy + dy;
            if (st.paint_mode == 3) {
                sim.erase_cell({x, y});
            } else {
                fs::Element e;
                e.base_id = paint_id;
                auto base = registry->get(paint_id);
                if (base.has_value() && base->get().color_func) {
                    e.color = base->get().color_func(seed_base + static_cast<std::uint64_t>(dy * 64 + dx));
                } else {
                    e.color = glm::vec4(1.0f);
                }
                sim.put_cell({x, y}, e);
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Tiny imgui status panel.
// ──────────────────────────────────────────────────────────────────────────────
void status_ui(imgui::Ctx imgui_ctx, ResMut<AppState> app_state) {
    (void)imgui_ctx;
    auto& st = *app_state;
    ImGui::Begin("Pixel Body Demo");
    ImGui::Text("LMB drag: paint selected element");
    ImGui::Text("RMB: spawn pixel body");
    ImGui::Text("MMB drag: pan camera");
    ImGui::Text("Scroll: zoom");
    ImGui::Separator();
    ImGui::RadioButton("Sand", &st.paint_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Water", &st.paint_mode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Stone", &st.paint_mode, 2);
    ImGui::SameLine();
    ImGui::RadioButton("Erase", &st.paint_mode, 3);
    ImGui::SliderInt("Brush radius", &st.paint_radius, 1, 16);
    ImGui::Separator();
    ImGui::TextUnformatted("Debug overlays");
    ImGui::Checkbox("Dirty rects", &st.show_dirty_rects);
    ImGui::Checkbox("Freefall cells", &st.show_freefall);
    ImGui::Checkbox("Chunk chain outlines", &st.show_chunk_chains);
    ImGui::Checkbox("Pixel body outlines", &st.show_body_outlines);
    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────────────
// Debug overlay helpers / systems.
// ──────────────────────────────────────────────────────────────────────────────

namespace {
inline void append_thick_segment(std::vector<glm::vec3>& positions,
                                 std::vector<glm::vec4>& colors,
                                 std::vector<std::uint32_t>& indices,
                                 glm::vec2 a,
                                 glm::vec2 b,
                                 float thickness,
                                 glm::vec4 color) {
    glm::vec2 d = b - a;
    float len   = glm::length(d);
    if (len < 1e-6f) return;
    glm::vec2 n        = glm::vec2(-d.y, d.x) / len * (thickness * 0.5f);
    std::uint32_t base = static_cast<std::uint32_t>(positions.size());
    positions.push_back(glm::vec3(a + n, 0.0f));
    positions.push_back(glm::vec3(a - n, 0.0f));
    positions.push_back(glm::vec3(b - n, 0.0f));
    positions.push_back(glm::vec3(b + n, 0.0f));
    for (int i = 0; i < 4; ++i) colors.push_back(color);
    indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
}
}  // namespace

void dirty_rect_overlay_system(
    Commands cmd,
    ResMut<AppState> app_state,
    ResMut<assets::Assets<mesh::Mesh>> meshes,
    Query<Item<const fs::SandWorld&>, With<fs::SimulatedByPlugin>> worlds,
    ParamSet<Query<Item<Entity, const fs::SandChunkPos&, const fs::SandChunkDirtyRect&, const transform::Transform&>>,
             Query<Item<Mut<transform::Transform>>, With<DirtyRectOverlay>>> qs) {
    auto& st = *app_state;
    if (!st.show_dirty_rects) {
        for (auto& [c, o] : st.dirty_rect_overlays) cmd.entity(o).despawn();
        st.dirty_rect_overlays.clear();
        return;
    }
    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world] = *world_opt;
    float cell_size     = sand_world.cell_size();

    auto chunks = std::get<0>(qs.get());
    std::unordered_map<Entity, std::pair<glm::vec3, fs::SandChunkDirtyRect>> active;
    for (auto&& [e, pos, rect, tf] : chunks.iter()) {
        if (!rect.active()) continue;
        active.emplace(e, std::pair{tf.translation, rect});
    }

    std::vector<Entity> to_remove;
    for (auto& [c, o] : st.dirty_rect_overlays) {
        if (!active.contains(c)) {
            cmd.entity(o).despawn();
            to_remove.push_back(c);
        }
    }
    for (auto e : to_remove) st.dirty_rect_overlays.erase(e);

    for (auto& [c, ar] : active) {
        auto& [origin, rect] = ar;
        float x0             = origin.x + static_cast<float>(rect.xmin) * cell_size;
        float x1             = origin.x + static_cast<float>(rect.xmax + 1) * cell_size;
        float y0             = origin.y + static_cast<float>(rect.ymin) * cell_size;
        float y1             = origin.y + static_cast<float>(rect.ymax + 1) * cell_size;
        glm::vec3 center{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 1.0f};
        glm::vec3 scaler{x1 - x0, y1 - y0, 1.0f};
        auto overlay_transforms = std::get<1>(qs.get());
        if (auto it = st.dirty_rect_overlays.find(c); it != st.dirty_rect_overlays.end()) {
            if (auto opt = overlay_transforms.get(it->second)) {
                auto&& [tf]              = *opt;
                tf.get_mut().translation = center;
                tf.get_mut().scaler      = scaler;
            }
        } else {
            auto handle               = meshes->emplace(mesh::make_box2d(1.0f, 1.0f));
            auto e                    = cmd.spawn(DirtyRectOverlay{}, mesh::Mesh2d{handle},
                                                  mesh::MeshMaterial2d{.color      = {1.0f, 0.3f, 0.1f, 0.30f},
                                                                       .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                                                  transform::Transform{.translation = center, .scaler = scaler})
                                            .id();
            st.dirty_rect_overlays[c] = e;
        }
    }
}

void freefall_overlay_system(Commands cmd,
                             ResMut<AppState> app_state,
                             ResMut<assets::Assets<mesh::Mesh>> meshes,
                             Query<Item<const fs::SandWorld&>, With<fs::SimulatedByPlugin>> worlds,
                             Query<Item<Entity, const ext::grid::Chunk<fs::kDim>&, const transform::Transform&>> chunks,
                             Query<Item<Mut<mesh::Mesh2d>>, With<FreefallOverlay>> overlay_meshes) {
    auto& st = *app_state;
    if (!st.show_freefall) {
        for (auto& [c, o] : st.freefall_overlays) cmd.entity(o).despawn();
        st.freefall_overlays.clear();
        return;
    }
    auto world_opt = worlds.single();
    if (!world_opt.has_value()) return;
    auto&& [sand_world] = *world_opt;
    float cell_size     = sand_world.cell_size();

    auto build_mesh = [&](const ext::grid::Chunk<fs::kDim>& chunk) {
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
            for (int i = 0; i < 4; ++i) colors.push_back(col);
            indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
            base += 4;
        }
        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint32_t>(indices);
    };

    std::vector<Entity> to_remove;
    for (auto& [c, o] : st.freefall_overlays) {
        if (!chunks.get(c).has_value()) {
            cmd.entity(o).despawn();
            to_remove.push_back(c);
        }
    }
    for (auto e : to_remove) st.freefall_overlays.erase(e);

    for (auto&& [c, chunk, tf] : chunks.iter()) {
        auto handle = meshes->emplace(build_mesh(chunk));
        if (auto it = st.freefall_overlays.find(c); it != st.freefall_overlays.end()) {
            if (auto opt = overlay_meshes.get(it->second)) {
                auto&& [m2d]  = *opt;
                m2d.get_mut() = mesh::Mesh2d{handle};
            }
        } else {
            auto e = cmd.spawn(FreefallOverlay{}, mesh::Mesh2d{handle},
                               mesh::MeshMaterial2d{.color      = {0.0f, 0.9f, 1.0f, 0.45f},
                                                    .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                               transform::Transform{.translation = tf.translation + glm::vec3{0.0f, 0.0f, 0.5f}})
                         .id();
            st.freefall_overlays[c] = e;
        }
    }
}

void chunk_chain_overlay_system(
    Commands cmd,
    ResMut<AppState> app_state,
    ResMut<assets::Assets<mesh::Mesh>> meshes,
    ParamSet<Query<Item<Entity, const pb::SandStaticBody&, const transform::Transform&>>,
             Query<Item<Mut<mesh::Mesh2d>, Mut<transform::Transform>>, With<ChunkChainOverlay>>> qs) {
    auto& st = *app_state;
    if (!st.show_chunk_chains) {
        for (auto& [c, o] : st.chunk_chain_overlays) cmd.entity(o).despawn();
        st.chunk_chain_overlays.clear();
        return;
    }
    constexpr float thickness = 0.6f;
    glm::vec4 color{1.0f, 1.0f, 0.2f, 0.95f};

    auto build_mesh = [&](const pb::SandStaticBody& ssb) {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        for (auto cid : ssb.chains) {
            if (!B2_IS_NON_NULL(cid)) continue;
            int seg_count = b2Chain_GetSegmentCount(cid);
            if (seg_count <= 0) continue;
            std::vector<b2ShapeId> seg_ids(static_cast<std::size_t>(seg_count));
            b2Chain_GetSegments(cid, seg_ids.data(), seg_count);
            for (auto sid : seg_ids) {
                if (b2Shape_GetType(sid) != b2_chainSegmentShape) continue;
                b2ChainSegment cs = b2Shape_GetChainSegment(sid);
                glm::vec2 a{cs.segment.point1.x, cs.segment.point1.y};
                glm::vec2 b{cs.segment.point2.x, cs.segment.point2.y};
                append_thick_segment(positions, colors, indices, a, b, thickness, color);
            }
        }
        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint32_t>(indices);
    };

    std::vector<Entity> to_remove;
    {
        auto chunks = std::get<0>(qs.get());
        for (auto& [c, o] : st.chunk_chain_overlays) {
            if (!chunks.get(c).has_value()) {
                cmd.entity(o).despawn();
                to_remove.push_back(c);
            }
        }
    }
    for (auto e : to_remove) st.chunk_chain_overlays.erase(e);

    struct ChainEntry {
        Entity entity;
        const pb::SandStaticBody* ssb;
    };
    std::vector<ChainEntry> active;
    {
        auto chunks = std::get<0>(qs.get());
        for (auto&& [c, ssb, tf] : chunks.iter()) {
            if (!B2_IS_NON_NULL(ssb.b2_body)) continue;
            active.push_back({c, &ssb});
        }
    }
    auto overlays = std::get<1>(qs.get());
    for (auto& entry : active) {
        auto handle = meshes->emplace(build_mesh(*entry.ssb));
        if (auto it = st.chunk_chain_overlays.find(entry.entity); it != st.chunk_chain_overlays.end()) {
            if (auto opt = overlays.get(it->second)) {
                auto&& [m2d, t]         = *opt;
                m2d.get_mut()           = mesh::Mesh2d{handle};
                t.get_mut().translation = glm::vec3{0.0f, 0.0f, 1.5f};
            }
        } else {
            auto e = cmd.spawn(ChunkChainOverlay{}, mesh::Mesh2d{handle},
                               mesh::MeshMaterial2d{.color = color, .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                               transform::Transform{.translation = glm::vec3{0.0f, 0.0f, 1.5f}})
                         .id();
            st.chunk_chain_overlays[entry.entity] = e;
        }
    }
}

void body_outline_overlay_system(
    Commands cmd,
    ResMut<AppState> app_state,
    ResMut<assets::Assets<mesh::Mesh>> meshes,
    ParamSet<Query<Item<Entity, const pb::PixelBody&, const transform::Transform&>>,
             Query<Item<Mut<mesh::Mesh2d>, Mut<transform::Transform>>, With<BodyOutlineOverlay>>> qs) {
    auto& st = *app_state;
    if (!st.show_body_outlines) {
        for (auto& [c, o] : st.body_outline_overlays) cmd.entity(o).despawn();
        st.body_outline_overlays.clear();
        return;
    }
    constexpr float thickness = 0.4f;
    glm::vec4 color{0.2f, 1.0f, 0.4f, 0.95f};

    auto build_mesh = [&](const pb::PixelBody& body) {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        for (auto sid : body.shapes) {
            if (!B2_IS_NON_NULL(sid)) continue;
            if (b2Shape_GetType(sid) != b2_polygonShape) continue;
            b2Polygon poly = b2Shape_GetPolygon(sid);
            for (int i = 0; i < poly.count; ++i) {
                int j = (i + 1) % poly.count;
                glm::vec2 a{poly.vertices[i].x, poly.vertices[i].y};
                glm::vec2 b{poly.vertices[j].x, poly.vertices[j].y};
                append_thick_segment(positions, colors, indices, a, b, thickness, color);
            }
        }
        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint32_t>(indices);
    };

    std::vector<Entity> to_remove;
    {
        auto bodies = std::get<0>(qs.get());
        for (auto& [c, o] : st.body_outline_overlays) {
            if (!bodies.get(c).has_value()) {
                cmd.entity(o).despawn();
                to_remove.push_back(c);
            }
        }
    }
    for (auto e : to_remove) st.body_outline_overlays.erase(e);

    struct BodyEntry {
        Entity entity;
        const pb::PixelBody* body;
        glm::vec3 translation;
        glm::quat rotation;
    };
    std::vector<BodyEntry> active;
    {
        auto bodies = std::get<0>(qs.get());
        for (auto&& [c, body, tf] : bodies.iter()) {
            if (!B2_IS_NON_NULL(body.b2_body)) continue;
            active.push_back({c, &body, tf.translation, tf.rotation});
        }
    }
    auto overlays = std::get<1>(qs.get());
    for (auto& entry : active) {
        auto handle = meshes->emplace(build_mesh(*entry.body));
        glm::vec3 t = entry.translation + glm::vec3{0.0f, 0.0f, 2.0f};
        if (auto it = st.body_outline_overlays.find(entry.entity); it != st.body_outline_overlays.end()) {
            if (auto opt = overlays.get(it->second)) {
                auto&& [m2d, ovt] = *opt;
                m2d.get_mut()     = mesh::Mesh2d{handle};
                ovt.get_mut()     = transform::Transform{.translation = t, .rotation = entry.rotation};
            }
        } else {
            auto e = cmd.spawn(BodyOutlineOverlay{}, mesh::Mesh2d{handle},
                               mesh::MeshMaterial2d{.color = color, .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                               transform::Transform{.translation = t, .rotation = entry.rotation})
                         .id();
            st.body_outline_overlays[entry.entity] = e;
        }
    }
}

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Pixel Body + Falling Sand demo";
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
        .add_plugins(pb::PixelBodyPlugin{})
        .add_systems(PreStartup, into(setup).set_name("pixel_body_sand setup"))
        .add_systems(Startup, into(seed).set_name("pixel_body_sand seed"))
        .add_systems(PreUpdate, into(status_ui).after(imgui::BeginFrameSet))
        .add_systems(Update, into(camera_control))
        .add_systems(Update, into(spawn_body_on_click))
        .add_systems(Update, into(paint_sand_on_drag))
        .add_systems(time::FixedPostUpdate, into(dirty_rect_overlay_system))
        .add_systems(time::FixedPostUpdate, into(freefall_overlay_system))
        .add_systems(PostUpdate, into(chunk_chain_overlay_system))
        .add_systems(PostUpdate, into(body_outline_overlay_system));

    app.run();
}
