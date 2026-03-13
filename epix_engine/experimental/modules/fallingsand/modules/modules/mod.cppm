export module epix.experimental.fallingsand;

import std;
import glm;

import epix.assets;
import epix.core;
import epix.core_graph;
import epix.mesh;
import epix.transform;

import epix.experimental.grid;

namespace ext::fallingsand {
namespace {
constexpr std::size_t kDim = 2;

struct SandSimulation {
    grid::ExtendibleChunkGrid<kDim> grid;
    std::int32_t width;
    std::int32_t height;
    std::size_t chunk_shift;

    SandSimulation(std::int32_t world_width, std::int32_t world_height, std::size_t world_chunk_shift)
        : grid(world_chunk_shift), width(world_width), height(world_height), chunk_shift(world_chunk_shift) {
        std::int32_t chunk_width = static_cast<std::int32_t>(grid.chunk_width());
        std::int32_t chunks_x    = (width + chunk_width - 1) / chunk_width;
        std::int32_t chunks_y    = (height + chunk_width - 1) / chunk_width;
        for (std::int32_t cy = 0; cy < chunks_y; ++cy) {
            for (std::int32_t cx = 0; cx < chunks_x; ++cx) {
                grid::Chunk<kDim> chunk(chunk_shift);
                auto layer_result = chunk.add_layer(std::make_unique<grid::layers::DenseLayer<kDim, int>>(chunk_shift));
                if (!layer_result.has_value()) {
                    continue;
                }
                (void)grid.insert_chunk({cx, cy}, std::move(chunk));
            }
        }
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
    std::uint32_t rng = 0x8badf00dU;
    std::uint64_t frame{0};
};

export struct SimpleFallingSandPlugin {
    SimpleFallingSandSettings settings{};

    void build(core::App& app) {
        auto cfg = settings;
        app.add_systems(
            core::PreStartup, core::into([cfg](core::Commands cmd, core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                auto sim = SandSimulation(cfg.width, cfg.height, cfg.chunk_shift);
                sim.seed_pile();
                auto mesh_handle = meshes->emplace(sim.build_mesh(cfg.cell_size));

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
            core::into([](core::ResMut<SimpleFallingSandState> state, core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                state->frame += 1;
                state->rng          = state->rng * 1664525U + 1013904223U;
                std::int32_t center = state->sim.width / 2 + static_cast<std::int32_t>((state->rng % 11U) - 5U);
                state->sim.spawn_row_drop(center, 2);

                // Run multiple substeps to keep motion visible at lower frame rates.
                state->sim.step(state->frame);
                state->sim.step(state->frame + 1);

                (void)meshes->insert(state->mesh.id(), state->sim.build_mesh(state->cell_size));
            }).set_name("falling sand simulate and mesh sync"));
    }
};
}  // namespace ext::fallingsand