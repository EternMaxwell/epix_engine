#include <imgui.h>
#ifndef EPIX_IMPORT_STD
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
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
// Per-example state shared between systems.
// ──────────────────────────────────────────────────────────────────────────────
struct SandAppState {
    std::size_t sand_base_id  = 0;
    std::int32_t brush_radius = 2;
};

// Helper: build a transient SandSimulation for this world, or nullopt on error.
static std::optional<fs::SandSimulation> make_sim(
    fs::SandWorld& world,
    const fs::ElementRegistry& registry,
    std::optional<std::reference_wrapper<const Children>> maybe_children,
    Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, const Parent&>>& all_chunks) {
    if (!maybe_children.has_value()) return std::nullopt;
    const auto& child_entities = maybe_children->get().entities();
    auto chunk_range = child_entities |
                       std::views::filter([&all_chunks](Entity e) { return all_chunks.get(e).has_value(); }) |
                       std::views::transform(
                           [&all_chunks](Entity e) -> std::tuple<ext::grid::Chunk<fs::kDim>&, const fs::SandChunkPos&> {
                               auto o                       = all_chunks.get(e);
                               auto&& [chunk, pos, par_ref] = *o;
                               return {chunk.get_mut(), pos};
                           });
    auto result      = fs::SandSimulation::create(world, registry, chunk_range);
    if (!result.has_value()) return std::nullopt;
    return std::move(*result);
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-frame imgui settings panel.
// ──────────────────────────────────────────────────────────────────────────────
void settings_ui(imgui::Ctx imgui_ctx,
                 ResMut<SandAppState> app_state,
                 Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
                 Res<fs::ElementRegistry> registry,
                 Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, const Parent&>> all_chunks) {
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
    if (ImGui::Button("Clear (C)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks))
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset (R)")) {
        if (auto sim = make_sim(w, registry.get(), maybe_children, all_chunks)) {
            for (auto&& chunk : sim->iter_chunks_mut()) chunk.get().clear();
            const auto& base = registry.get()[st.sand_base_id];
            for (std::int64_t y = -4; y <= 4; ++y)
                for (std::int64_t x = -8; x <= 8; ++x) {
                    std::uint64_t seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                         static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                    (void)sim->insert_cell({x, y}, fs::Element{st.sand_base_id, base.color_func(seed)});
                }
        }
    }
    ImGui::Separator();
    ImGui::Text("R = reset, C = clear, Q/E = brush, Space = pause");

    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────────────
// Input system: keyboard shortcuts and mouse painting.
// ──────────────────────────────────────────────────────────────────────────────
void input_system(ResMut<SandAppState> app_state_mut,
                  Res<fs::ElementRegistry> registry,
                  Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
                  Res<input::ButtonInput<input::KeyCode>> keys,
                  Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                  Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
                  Query<Item<const render::camera::Camera&, const transform::Transform&>> cameras,
                  Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, const Parent&>> all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto& w                             = sand_world.get_mut();
    auto& st                            = app_state_mut.get_mut();

    // ── Settings that don't need a simulation ────────────────────────────────
    if (keys->just_pressed(input::KeyCode::KeySpace)) w.set_paused(!w.paused());
    if (keys->just_pressed(input::KeyCode::KeyQ)) st.brush_radius = std::max(1, st.brush_radius - 1);
    if (keys->just_pressed(input::KeyCode::KeyE)) st.brush_radius = std::min(32, st.brush_radius + 1);

    // ── Actions that need a transient SandSimulation ─────────────────────────
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
        const auto& base = registry.get()[st.sand_base_id];
        for (std::int64_t y = -4; y <= 4; ++y)
            for (std::int64_t x = -8; x <= 8; ++x) {
                std::uint64_t seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                     static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                (void)sim->insert_cell({x, y}, fs::Element{st.sand_base_id, base.color_func(seed)});
            }
    }

    // ── Mouse painting ───────────────────────────────────────────────────────
    if (lmb || rmb) {
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
        std::size_t sid  = st.sand_base_id;

        if (lmb) {
            const auto& base = registry.get()[sid];
            std::int32_t r2  = br * br;
            for (std::int32_t dy = -br; dy <= br; ++dy)
                for (std::int32_t dx = -br; dx <= br; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    std::int32_t px = cx_ + dx, py = cy_ + dy;
                    std::uint64_t seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32) |
                                         static_cast<std::uint64_t>(static_cast<std::uint32_t>(py));
                    (void)sim->insert_cell({px, py}, fs::Element{sid, base.color_func(seed)});
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
void seed(Query<Item<Mut<fs::SandWorld>, Opt<const Children&>>, With<fs::SimulatedByPlugin>> worlds,
          Res<fs::ElementRegistry> registry,
          Res<SandAppState> app_state,
          Query<Item<Mut<ext::grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, const Parent&>> all_chunks) {
    auto opt = worlds.single();
    if (!opt.has_value()) return;
    auto&& [sand_world, maybe_children] = *opt;
    auto sim                            = make_sim(sand_world.get_mut(), registry.get(), maybe_children, all_chunks);
    if (!sim.has_value()) return;
    const auto& base = registry.get()[app_state.get().sand_base_id];
    for (std::int64_t y = -4; y <= 4; ++y)
        for (std::int64_t x = -8; x <= 8; ++x) {
            std::uint64_t seed_v = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                   static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
            (void)sim->insert_cell({x, y}, fs::Element{app_state.get().sand_base_id, base.color_func(seed_v)});
        }
}

// ──────────────────────────────────────────────────────────────────────────────
// Startup: spawn camera, sand world entity, and chunk children.
// ──────────────────────────────────────────────────────────────────────────────
void setup(Commands cmd) {
    // ── Register elements and emit SandAppState resource ────────────────────────
    fs::ElementRegistry registry;
    std::size_t sand_id = 0;
    {
        auto res = registry.register_element(fs::ElementBase{
            .name    = "sand",
            .density = 1.0f,
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
        });
        sand_id  = res.value_or(0);
    }
    cmd.insert_resource(std::move(registry));
    cmd.insert_resource(SandAppState{sand_id, 2});

    cmd.spawn(core_graph::core_2d::Camera2DBundle{});

    constexpr std::size_t chunk_shift = 5;
    constexpr float cell_size         = 4.0f;
    constexpr std::int32_t chunk_w    = static_cast<std::int32_t>(std::size_t(1) << chunk_shift);
    constexpr std::int32_t chunks_x   = 4;
    constexpr std::int32_t chunks_y   = 3;

    // Spawn the world entity — FallingSandPlugin picks it up via its components.
    auto world_entity = cmd.spawn(fs::SandWorld(chunk_shift, cell_size), transform::Transform{},
                                  fs::SimulatedByPlugin{}, fs::MeshBuildByPlugin{});

    // Spawn chunk children.
    for (std::int32_t cy = -chunks_y; cy < chunks_y; ++cy) {
        for (std::int32_t cx = -chunks_x; cx < chunks_x; ++cx) {
            ext::grid::Chunk<fs::kDim> chunk(chunk_shift);
            auto layer_res =
                chunk.add_layer(std::make_unique<ext::grid::layers::TreeLayer<fs::kDim, fs::Element>>(chunk_shift));
            if (!layer_res.has_value()) continue;

            world_entity.spawn(std::move(chunk), fs::SandChunkPos{{cx, cy}},
                               transform::Transform{
                                   .translation = glm::vec3(static_cast<float>(cx * chunk_w) * cell_size,
                                                            static_cast<float>(cy * chunk_w) * cell_size, 0.0f),
                               });
        }
    }
}

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Falling Sand (LMB add, RMB erase, Space pause, R reset, C clear)";
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
        .add_systems(Update, into(input_system));

    app.run();
}
