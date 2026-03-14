export module epix.experimental.fallingsand;

import std;
import glm;

import epix.assets;
import epix.core;
import epix.core_graph;
import epix.mesh;
import epix.render;
import epix.transform;
import epix.input;
import epix.window;

import epix.experimental.grid;

namespace ext::fallingsand {
namespace {
constexpr std::size_t kDim = 2;

glm::vec2 relative_to_world(glm::vec2 relative_pos,
                            const render::camera::Camera& camera,
                            const render::camera::Projection& projection,
                            const transform::Transform& cam_transform) {
    (void)projection;
    float ndc_x = relative_pos.x * 2.0f;
    float ndc_y = relative_pos.y * 2.0f;

    glm::mat4 proj_matrix = camera.computed.projection;
    glm::mat4 view_matrix = glm::inverse(cam_transform.to_matrix());
    glm::mat4 vp_inv      = glm::inverse(proj_matrix * view_matrix);

    glm::vec4 world = vp_inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return glm::vec2(world.x / world.w, world.y / world.w);
}

struct SandSimulation {
    grid::ExtendibleChunkRefGrid<kDim> grid;
    std::int32_t width;
    std::int32_t height;
    std::size_t chunk_shift;

    SandSimulation(std::int32_t world_width, std::int32_t world_height, std::size_t world_chunk_shift)
        : grid(world_chunk_shift), width(world_width), height(world_height), chunk_shift(world_chunk_shift) {}

    void reset_chunk_refs() { grid = grid::ExtendibleChunkRefGrid<kDim>(chunk_shift); }

    void add_chunk_ref(std::array<std::int32_t, kDim> chunk_pos, grid::Chunk<kDim>& chunk) {
        (void)grid.insert_chunk(chunk_pos, chunk);
    }

    bool in_bounds(std::int32_t x, std::int32_t y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    bool has_cell(std::int32_t x, std::int32_t y) const {
        if (!in_bounds(x, y)) return false;
        return grid.get_cell<int>({x, y}).has_value();
    }

    void set_cell(std::int32_t x, std::int32_t y) {
        if (!in_bounds(x, y)) return;
        (void)grid.insert_cell({x, y}, 1);
    }

    void clear_cell(std::int32_t x, std::int32_t y) {
        if (!in_bounds(x, y)) return;
        (void)grid.remove<int>({x, y});
    }

    void move_cell(std::int32_t from_x, std::int32_t from_y, std::int32_t to_x, std::int32_t to_y) {
        if (!in_bounds(from_x, from_y) || !in_bounds(to_x, to_y)) return;
        clear_cell(from_x, from_y);
        set_cell(to_x, to_y);
    }

    void seed_pile() {
        std::int32_t center = width / 2;
        for (std::int32_t y = height - 18; y < height - 4; ++y) {
            std::int32_t spread = (height - 4 - y) / 2;
            for (std::int32_t x = center - spread; x <= center + spread; ++x) {
                set_cell(x, y);
            }
        }
    }

    void spawn_row_drop(std::int32_t center, std::int32_t radius) {
        std::int32_t y = height - 1;
        for (std::int32_t x = center - radius; x <= center + radius; ++x) {
            if (in_bounds(x, y) && !has_cell(x, y)) set_cell(x, y);
        }
    }

    void clear_all() {
        for (std::int32_t y = 0; y < height; ++y) {
            for (std::int32_t x = 0; x < width; ++x) {
                clear_cell(x, y);
            }
        }
    }

    void paint_disc(std::int32_t center_x, std::int32_t center_y, std::int32_t radius, bool fill) {
        std::int32_t r2 = radius * radius;
        for (std::int32_t y = center_y - radius; y <= center_y + radius; ++y) {
            for (std::int32_t x = center_x - radius; x <= center_x + radius; ++x) {
                std::int32_t dx = x - center_x;
                std::int32_t dy = y - center_y;
                if (dx * dx + dy * dy > r2) continue;
                if (fill) {
                    set_cell(x, y);
                } else {
                    clear_cell(x, y);
                }
            }
        }
    }

    void step(std::uint64_t frame) {
        bool left_first = (frame & 1ULL) == 0ULL;
        for (std::int32_t y = 1; y < height; ++y) {
            for (std::int32_t x = 0; x < width; ++x) {
                if (!has_cell(x, y)) continue;
                if (!has_cell(x, y - 1)) {
                    move_cell(x, y, x, y - 1);
                    continue;
                }
                if (left_first) {
                    if (in_bounds(x - 1, y - 1) && !has_cell(x - 1, y - 1)) {
                        move_cell(x, y, x - 1, y - 1);
                    } else if (in_bounds(x + 1, y - 1) && !has_cell(x + 1, y - 1)) {
                        move_cell(x, y, x + 1, y - 1);
                    }
                } else {
                    if (in_bounds(x + 1, y - 1) && !has_cell(x + 1, y - 1)) {
                        move_cell(x, y, x + 1, y - 1);
                    } else if (in_bounds(x - 1, y - 1) && !has_cell(x - 1, y - 1)) {
                        move_cell(x, y, x - 1, y - 1);
                    }
                }
            }
        }
    }

    mesh::Mesh build_mesh(float cell_size) const {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        positions.reserve(static_cast<std::size_t>(width * height / 2) * 4ULL);
        colors.reserve(static_cast<std::size_t>(width * height / 2) * 4ULL);
        indices.reserve(static_cast<std::size_t>(width * height / 2) * 6ULL);

        auto half_x = static_cast<float>(width) * cell_size * 0.5f;
        auto half_y = static_cast<float>(height) * cell_size * 0.5f;

        for (std::int32_t gy = 0; gy < height; ++gy) {
            for (std::int32_t gx = 0; gx < width; ++gx) {
                if (!has_cell(gx, gy)) continue;

                float x0 = static_cast<float>(gx) * cell_size - half_x;
                float y0 = static_cast<float>(gy) * cell_size - half_y;
                float x1 = x0 + cell_size;
                float y1 = y0 + cell_size;

                std::uint32_t base = static_cast<std::uint32_t>(positions.size());
                positions.push_back({x0, y0, 0.0f});
                positions.push_back({x1, y0, 0.0f});
                positions.push_back({x1, y1, 0.0f});
                positions.push_back({x0, y1, 0.0f});

                // Subtle variation by height gives depth without extra materials.
                float shade = 0.75f + 0.25f * (static_cast<float>(gy) / static_cast<float>(std::max(1, height - 1)));
                glm::vec4 c(0.92f * shade, 0.74f * shade, 0.28f * shade, 1.0f);
                colors.push_back(c);
                colors.push_back(c);
                colors.push_back(c);
                colors.push_back(c);

                indices.push_back(base + 0);
                indices.push_back(base + 1);
                indices.push_back(base + 2);
                indices.push_back(base + 2);
                indices.push_back(base + 3);
                indices.push_back(base + 0);
            }
        }

        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint32_t>(indices);
    }
};

struct SandChunkPos {
    std::array<std::int32_t, kDim> value;
};
}  // namespace

export struct SimpleFallingSandSettings {
    std::int32_t width      = 128;
    std::int32_t height     = 96;
    std::size_t chunk_shift = 5;
    float cell_size         = 6.0f;
};

export struct SimpleFallingSandState {
    SandSimulation sim;
    assets::Handle<mesh::Mesh> mesh;
    float cell_size;
    bool initialized          = false;
    std::int32_t brush_radius = 3;
    bool paused               = false;
    bool auto_spawn           = true;
    std::uint32_t rng         = 0x8badf00dU;
    std::uint64_t frame{0};
};

export struct SimpleFallingSandPlugin {
    SimpleFallingSandSettings settings{};

    void build(core::App& app) {
        auto cfg = settings;
        app.add_systems(
            core::PreStartup, core::into([cfg](core::Commands cmd, core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                auto sim = SandSimulation(cfg.width, cfg.height, cfg.chunk_shift);

                std::int32_t chunk_width = static_cast<std::int32_t>(sim.grid.chunk_width());
                std::int32_t chunks_x    = (cfg.width + chunk_width - 1) / chunk_width;
                std::int32_t chunks_y    = (cfg.height + chunk_width - 1) / chunk_width;
                for (std::int32_t cy = 0; cy < chunks_y; ++cy) {
                    for (std::int32_t cx = 0; cx < chunks_x; ++cx) {
                        grid::Chunk<kDim> chunk(cfg.chunk_shift);
                        auto layer_result =
                            chunk.add_layer(std::make_unique<grid::layers::DenseLayer<kDim, int>>(cfg.chunk_shift));
                        if (!layer_result.has_value()) continue;
                        cmd.spawn(SandChunkPos{.value = {cx, cy}}, std::move(chunk));
                    }
                }

                auto mesh_handle =
                    meshes->emplace(mesh::Mesh().with_primitive_type(wgpu::PrimitiveTopology::eTriangleList));

                cmd.spawn(core_graph::core_2d::Camera2DBundle{});
                cmd.spawn(mesh::Mesh2d{mesh_handle},
                          mesh::MeshMaterial2d{.color = glm::vec4(1.0f), .alpha_mode = mesh::MeshAlphaMode2d::Opaque},
                          transform::Transform{});

                cmd.insert_resource(SimpleFallingSandState{
                    .sim       = std::move(sim),
                    .mesh      = mesh_handle,
                    .cell_size = cfg.cell_size,
                });
            }));

        app.add_systems(
            core::Update,
            core::into(
                [](core::ResMut<SimpleFallingSandState> state, core::Res<input::ButtonInput<input::KeyCode>> keys,
                   core::Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                   core::Query<core::Item<const SandChunkPos&, grid::Chunk<kDim>&>> chunk_query,
                   core::Query<core::Item<const render::camera::Camera&, const render::camera::Projection&,
                                          const transform::Transform&>> camera_query,
                   core::Query<core::Item<const window::CachedWindow&>, core::With<window::PrimaryWindow>> window_query,
                   core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                    state->frame += 1;

                    state->sim.reset_chunk_refs();
                    for (auto&& [chunk_pos, chunk] : chunk_query.iter()) {
                        state->sim.add_chunk_ref(chunk_pos.value, chunk);
                    }

                    if (!state->initialized) {
                        state->sim.seed_pile();
                        state->initialized = true;
                    }

                    if (keys->just_pressed(input::KeyCode::KeySpace)) state->paused = !state->paused;
                    if (keys->just_pressed(input::KeyCode::KeyT)) state->auto_spawn = !state->auto_spawn;
                    if (keys->just_pressed(input::KeyCode::KeyQ) && state->brush_radius > 1) state->brush_radius -= 1;
                    if (keys->just_pressed(input::KeyCode::KeyE) && state->brush_radius < 16) state->brush_radius += 1;
                    if (keys->just_pressed(input::KeyCode::KeyC)) state->sim.clear_all();
                    if (keys->just_pressed(input::KeyCode::KeyR)) {
                        state->sim.clear_all();
                        state->sim.seed_pile();
                    }

                    if (auto window_opt = window_query.single(); window_opt.has_value()) {
                        if (auto camera_opt = camera_query.single(); camera_opt.has_value()) {
                            auto&& [window]                            = *window_opt;
                            auto&& [camera, projection, cam_transform] = *camera_opt;
                            auto [win_w, win_h]                        = window.size;
                            if (win_w > 0 && win_h > 0) {
                                auto [rel_x, rel_y] = window.relative_cursor_pos();
                                glm::vec2 world_cursor =
                                    relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)),
                                                      camera, projection, cam_transform);

                                float half_x = static_cast<float>(state->sim.width) * state->cell_size * 0.5f;
                                float half_y = static_cast<float>(state->sim.height) * state->cell_size * 0.5f;
                                float fx     = (world_cursor.x + half_x) / state->cell_size;
                                float fy     = (world_cursor.y + half_y) / state->cell_size;

                                std::int32_t gx = static_cast<std::int32_t>(std::floor(fx));
                                std::int32_t gy = static_cast<std::int32_t>(std::floor(fy));

                                if (state->sim.in_bounds(gx, gy)) {
                                    if (mouse_buttons->pressed(input::MouseButton::MouseButtonLeft)) {
                                        state->sim.paint_disc(gx, gy, state->brush_radius, true);
                                    }
                                    if (mouse_buttons->pressed(input::MouseButton::MouseButtonRight)) {
                                        state->sim.paint_disc(gx, gy, state->brush_radius, false);
                                    }
                                }
                            }
                        }
                    }

                    if (!state->paused) {
                        if (state->auto_spawn) {
                            state->rng = state->rng * 1664525U + 1013904223U;
                            std::int32_t center =
                                state->sim.width / 2 + static_cast<std::int32_t>((state->rng % 11U) - 5U);
                            state->sim.spawn_row_drop(center, 2);
                        }

                        // Run multiple substeps to keep motion visible at lower frame rates.
                        state->sim.step(state->frame);
                        state->sim.step(state->frame + 1);
                    }

                    (void)meshes->insert(state->mesh.id(), state->sim.build_mesh(state->cell_size));
                })
                .set_name("falling sand simulate and mesh sync"));
    }
};
}  // namespace ext::fallingsand