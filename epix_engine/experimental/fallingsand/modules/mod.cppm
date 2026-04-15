export module epix.experimental.fallingsand;

import std;
import glm;
import webgpu;
import BS.thread_pool;

import epix.assets;
import epix.core;
import epix.core_graph;
import epix.mesh;
import epix.render;
import epix.transform;
import epix.input;
import epix.window;
import epix.time;

import epix.extension.grid;

using namespace epix::core;

namespace epix::ext::fallingsand {
constexpr std::size_t kDim = 2;

export enum class ElementType {
    Solid,
    Powder,
    Liquid,
    Gas,
};

/**
 * @brief struct for storing common properties for some kind of elements.
 *
 * The actual element should be able to reference a base element.
 */
export struct ElementBase {
    std::string name;
    float density;
    ElementType type;
    std::function<glm::vec4(std::uint64_t sd)>
        color_func;  // color function that takes a seed and returns a color, used for procedural variation
    // add more properties as needed
};

export enum class ElementRegistryError {
    NameAlreadyExists,
    NameNotFound,
    InvalidBaseId,
};

export struct ElementRegistry {
   private:
    std::vector<ElementBase> elements;
    std::unordered_map<std::string, std::size_t> name_to_id;

   public:
    std::expected<std::size_t, ElementRegistryError> register_element(ElementBase element) {
        if (name_to_id.contains(element.name)) return std::unexpected(ElementRegistryError::NameAlreadyExists);
        std::size_t id = elements.size();
        elements.emplace_back(std::move(element));
        name_to_id.emplace(element.name, id);
        return id;
    }
    std::expected<std::reference_wrapper<const ElementBase>, ElementRegistryError> get(std::size_t id) const {
        if (id >= elements.size()) return std::unexpected(ElementRegistryError::InvalidBaseId);
        return std::cref(elements[id]);
    }
    std::expected<std::size_t, ElementRegistryError> get_id(const std::string& name) const {
        if (!name_to_id.contains(name)) return std::unexpected(ElementRegistryError::NameNotFound);
        return name_to_id.at(name);
    }
    std::expected<std::reference_wrapper<const ElementBase>, ElementRegistryError> get(const std::string& name) const {
        if (!name_to_id.contains(name)) return std::unexpected(ElementRegistryError::NameNotFound);
        return get(name_to_id.at(name));
    }
    const ElementBase& operator[](std::size_t id) const { return elements[id]; }  // TODO: contract check in cpp26
    auto iter() const {
        return std::views::transform(name_to_id, [&elements = this->elements](auto&& pair) {
            auto&& [name, id] = pair;
            return std::tuple<const std::string&, const ElementBase&>{name, elements[id]};
        });
    }
    auto iter_names() const { return std::views::keys(name_to_id); }
    auto iter_elements() const { return std::views::all(elements); }
};

export struct Element {
    std::size_t base_id;
    glm::vec4 color;
};

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

export struct SandChunkPos {
    std::array<std::int32_t, kDim> value;
};

export struct SandChunkMesh {};
export struct SandChunkOutline {};

export struct SandChunkRenderChildren {
    Entity mesh_entity;
    std::optional<Entity> outline_entity;
};

export struct SandSimulation : grid::ExtendibleChunkRefGrid<kDim> {
   private:
    std::unique_ptr<ElementRegistry> element_registry;
    std::unique_ptr<BS::thread_pool<>> thread_pool;
    std::size_t m_sand_base_id  = 0;
    float m_cell_size           = 6.0f;
    bool m_initialized          = false;
    bool m_paused               = false;
    bool m_auto_fall            = true;
    bool m_show_chunk_outlines  = true;
    std::int32_t m_brush_radius = 2;
    std::uint64_t m_tick        = 0;

    enum class CellState {
        Occupied,
        EmptyInChunk,
        Blocked,
    };

    CellState cell_state(std::int64_t x, std::int64_t y) const {
        auto cell = get_cell<Element>({x, y});
        if (cell.has_value()) return CellState::Occupied;

        return std::visit(
            [](auto&& error) -> CellState {
                using E = std::remove_cvref_t<decltype(error)>;
                if constexpr (std::same_as<E, grid::LayerError>) {
                    return error == grid::LayerError::EmptyCell ? CellState::EmptyInChunk : CellState::Blocked;
                }
                return CellState::Blocked;
            },
            cell.error());
    }

    bool has_cell(std::int64_t x, std::int64_t y) const { return cell_state(x, y) == CellState::Occupied; }

    bool set_cell(std::int64_t x, std::int64_t y, Element value) {
        return insert_cell({x, y}, std::move(value)).has_value();
    }

    bool clear_cell(std::int64_t x, std::int64_t y) { return remove_cell<Element>({x, y}).has_value(); }

    bool move_cell(std::int64_t from_x, std::int64_t from_y, std::int64_t to_x, std::int64_t to_y) {
        if (cell_state(to_x, to_y) != CellState::EmptyInChunk) return false;

        auto from = get_cell<Element>({from_x, from_y});
        if (!from.has_value()) return false;

        Element moved = from->get();
        if (!remove_cell<Element>({from_x, from_y}).has_value()) return false;
        if (!insert_cell({to_x, to_y}, std::move(moved)).has_value()) return false;
        return true;
    }

    std::optional<std::array<std::int64_t, 4>> cell_bounds() const {
        auto chunk_pos_iter = iter_chunk_pos();
        auto begin          = chunk_pos_iter.begin();
        auto end            = chunk_pos_iter.end();
        if (begin == end) return std::nullopt;

        const std::int64_t cw = static_cast<std::int64_t>(chunk_width());

        std::int64_t min_cx = std::numeric_limits<std::int64_t>::max();
        std::int64_t min_cy = std::numeric_limits<std::int64_t>::max();
        std::int64_t max_cx = std::numeric_limits<std::int64_t>::min();
        std::int64_t max_cy = std::numeric_limits<std::int64_t>::min();

        for (auto&& cpos : chunk_pos_iter) {
            min_cx = std::min(min_cx, static_cast<std::int64_t>(cpos[0]));
            min_cy = std::min(min_cy, static_cast<std::int64_t>(cpos[1]));
            max_cx = std::max(max_cx, static_cast<std::int64_t>(cpos[0]));
            max_cy = std::max(max_cy, static_cast<std::int64_t>(cpos[1]));
        }

        return std::array<std::int64_t, 4>{
            min_cx * cw,
            (max_cx + 1) * cw - 1,
            min_cy * cw,
            (max_cy + 1) * cw - 1,
        };
    }

    void seed_pile() {
        auto bounds = cell_bounds();
        if (!bounds.has_value()) return;

        const std::int64_t min_x = bounds->at(0);
        const std::int64_t max_x = bounds->at(1);
        const std::int64_t min_y = bounds->at(2);
        const std::int64_t max_y = bounds->at(3);

        const std::int64_t cx    = (min_x + max_x) / 2;
        const std::int64_t start = std::max(min_y, max_y - 8);
        for (std::int64_t y = start; y < max_y; ++y) {
            for (std::int64_t x = cx - 8; x <= cx + 8; ++x) {
                std::uint64_t seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                     static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                (void)set_cell(x, y, Element{m_sand_base_id, registry()[m_sand_base_id].color_func(seed)});
            }
        }
    }

    void step_cells() {
        const std::int64_t cw = static_cast<std::int64_t>(chunk_width());

        auto chunk_coords = std::ranges::to<std::vector>(iter_chunk_pos());
        static auto rng   = std::mt19937{std::random_device{}()};
        std::shuffle(chunk_coords.begin(), chunk_coords.end(), rng);

        for (auto&& [rx, ry] : std::views::cartesian_product(std::views::iota(0, 3), std::views::iota(0, 3))) {
            for (auto&& cpos : std::views::filter(chunk_coords, [rx, ry](auto&& cpos) {
                     auto&& [cx, cy] = cpos;
                     auto x_r        = (cx % 3 + 3) % 3;
                     auto y_r        = (cy % 3 + 3) % 3;
                     return x_r == rx && y_r == ry;
                 })) {
                thread_pool->detach_task([cpos, cw, this] {
                    for (auto&& [x, y] : std::views::transform(
                             std::views::elements<0>(get_chunk(cpos).value().get().iter<Element>()),
                             [cpos, cw](auto&& cell_pos) {
                                 auto&& [cx, cy] = cpos;
                                 auto&& [lx, ly] = cell_pos;
                                 return std::array<std::int64_t, 2>{
                                     static_cast<std::int64_t>(cx) * cw + static_cast<std::int64_t>(lx),
                                     static_cast<std::int64_t>(cy) * cw + static_cast<std::int64_t>(ly),
                                 };
                             })) {
                        if (!has_cell(x, y)) continue;

                        if (move_cell(x, y, x, y - 1)) continue;

                        const bool prefer_left = ((x + y + static_cast<std::int64_t>(m_tick)) & 1) == 0;
                        if (prefer_left) {
                            if (move_cell(x, y, x - 1, y - 1)) continue;
                            (void)move_cell(x, y, x + 1, y - 1);
                        } else {
                            if (move_cell(x, y, x + 1, y - 1)) continue;
                            (void)move_cell(x, y, x - 1, y - 1);
                        }
                    }
                });
            }
            thread_pool->wait();
        }

        m_tick++;
    }

   public:
    SandSimulation(std::size_t chunk_width_shift             = 5,
                   float cell_size                           = 4.0f,
                   std::unique_ptr<ElementRegistry> registry = std::make_unique<ElementRegistry>())
        : grid::ExtendibleChunkRefGrid<kDim>(chunk_width_shift),
          element_registry(std::move(registry)),
          m_cell_size(cell_size) {
        auto sand_res  = element_registry->register_element(ElementBase{
            .name    = "sand",
            .density = 1.0f,
            .type    = ElementType::Powder,
            .color_func =
                [](std::uint64_t sd) {
                    // splitmix64 avalanche — avoids the identity-hash problem on GCC/libstdc++
                    sd += 0x9e3779b97f4a7c15ULL;
                    sd = (sd ^ (sd >> 30)) * 0xbf58476d1ce4e5b9ULL;
                    sd = (sd ^ (sd >> 27)) * 0x94d049bb133111ebULL;
                    sd ^= (sd >> 31);
                    float t = static_cast<float>(sd) / static_cast<float>(std::numeric_limits<std::uint64_t>::max());
                    return glm::vec4(0.80f + t * 0.18f, 0.68f + t * 0.15f, 0.18f + t * 0.08f, 1.0f);
                },
        });
        m_sand_base_id = sand_res.value_or(0);
        thread_pool    = std::make_unique<BS::thread_pool<>>(std::thread::hardware_concurrency());
    }

    const ElementRegistry& registry() const { return *element_registry; }
    ElementRegistry& registry_mut() { return *element_registry; }

    float cell_size() const { return m_cell_size; }
    void set_cell_size(float size) { m_cell_size = size; }
    std::int32_t brush_radius() const { return m_brush_radius; }
    void set_brush_radius(std::int32_t r) { m_brush_radius = std::clamp(r, 1, 32); }
    bool paused() const { return m_paused; }
    void set_paused(bool p) { m_paused = p; }
    bool auto_fall() const { return m_auto_fall; }
    void set_auto_fall(bool a) { m_auto_fall = a; }
    bool show_chunk_outlines() const { return m_show_chunk_outlines; }
    void set_show_chunk_outlines(bool show) { m_show_chunk_outlines = show; }
    std::uint64_t tick() const { return m_tick; }

    void clear_all() {
        for (auto&& chunk : iter_chunks_mut()) chunk.get().clear();
    }

    void reset() {
        clear_all();
        seed_pile();
    }

    struct PaintDesc {
        std::int32_t center_x;
        std::int32_t center_y;
        std::int32_t radius;
        bool fill;
    };

    void paint_disc(PaintDesc desc, std::size_t base_id) {
        std::int32_t r2 = desc.radius * desc.radius;
        for (std::int32_t y = desc.center_y - desc.radius; y <= desc.center_y + desc.radius; ++y) {
            for (std::int32_t x = desc.center_x - desc.radius; x <= desc.center_x + desc.radius; ++x) {
                std::int32_t dx = x - desc.center_x;
                std::int32_t dy = y - desc.center_y;
                if (dx * dx + dy * dy > r2) continue;
                if (desc.fill) {
                    std::uint64_t seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                         static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                    (void)insert_cell({x, y}, Element{base_id, registry()[base_id].color_func(seed)});
                } else {
                    (void)clear_cell(x, y);
                }
            }
        }
    }

    static void step(
        ResMut<SandSimulation> sim,
        Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
        Res<input::ButtonInput<input::KeyCode>> keys,
        Query<Item<const window::CachedWindow&>, With<window::PrimaryWindow>> windows,
        Query<Item<const render::camera::Camera&, const render::camera::Projection&, const transform::Transform&>>
            cameras,
        Query<Item<grid::Chunk<kDim>&, const SandChunkPos&>> chunks) {
        // Build simulation view from ECS chunks.
        sim->clear_grid();
        for (auto&& [chunk, pos] : chunks.iter()) {
            (void)sim->insert_chunk(pos.value, chunk);
        }

        if (!sim->m_initialized) {
            sim->clear_all();
            sim->seed_pile();
            sim->m_initialized = true;
        }

        if (keys->just_pressed(input::KeyCode::KeySpace)) sim->set_paused(!sim->paused());
        if (keys->just_pressed(input::KeyCode::KeyT)) sim->set_auto_fall(!sim->auto_fall());
        if (keys->just_pressed(input::KeyCode::KeyQ)) sim->set_brush_radius(sim->brush_radius() - 1);
        if (keys->just_pressed(input::KeyCode::KeyE)) sim->set_brush_radius(sim->brush_radius() + 1);
        if (keys->just_pressed(input::KeyCode::KeyC)) sim->clear_all();
        if (keys->just_pressed(input::KeyCode::KeyR)) sim->reset();

        auto window = windows.single();
        auto camera = cameras.single();
        if (window.has_value() && camera.has_value()) {
            auto&& [primary_window]                 = *window;
            auto&& [cam, projection, cam_transform] = *camera;

            auto [rel_x, rel_y]    = primary_window.relative_cursor_pos();
            glm::vec2 world_cursor = relative_to_world(glm::vec2(static_cast<float>(rel_x), static_cast<float>(rel_y)),
                                                       cam, projection, cam_transform);

            std::int32_t cell_x = static_cast<std::int32_t>(std::floor(world_cursor.x / sim->cell_size()));
            std::int32_t cell_y = static_cast<std::int32_t>(std::floor(world_cursor.y / sim->cell_size()));

            if (mouse_buttons->pressed(input::MouseButton::MouseButtonLeft)) {
                sim->paint_disc({.center_x = cell_x, .center_y = cell_y, .radius = sim->brush_radius(), .fill = true},
                                sim->m_sand_base_id);
            }
            if (mouse_buttons->pressed(input::MouseButton::MouseButtonRight)) {
                sim->paint_disc({.center_x = cell_x, .center_y = cell_y, .radius = sim->brush_radius(), .fill = false},
                                sim->m_sand_base_id);
            }
        }

        if (sim->m_auto_fall && !sim->m_paused) {
            auto bounds = sim->cell_bounds();
            if (bounds.has_value()) {
                const std::int64_t cx = (bounds->at(0) + bounds->at(1)) / 2;
                const std::int64_t cy = std::max(bounds->at(2), bounds->at(3) - 1);
                sim->paint_disc({.center_x = static_cast<std::int32_t>(cx),
                                 .center_y = static_cast<std::int32_t>(cy),
                                 .radius   = 1,
                                 .fill     = true},
                                sim->m_sand_base_id);
            }
        }

        if (!sim->m_paused) {
            sim->step_cells();
        }
    }

    static mesh::Mesh build_chunk_mesh(const grid::Chunk<kDim>& chunk, float cell_size) {
        auto positions_view = std::views::join(std::views::transform(
            std::views::elements<0>(chunk.iter<Element>()), [cell_size](std::array<std::uint32_t, kDim> pos) {
                float x = static_cast<float>(pos[0]) * cell_size;
                float y = static_cast<float>(pos[1]) * cell_size;
                return std::array<glm::vec3, 4>{{{x, y, 0.0f},
                                                 {x + cell_size, y, 0.0f},
                                                 {x + cell_size, y + cell_size, 0.0f},
                                                 {x, y + cell_size, 0.0f}}};
            }));
        auto colors_view    = std::views::join(std::views::transform(chunk.iter<Element>(), [&](auto&& pair) {
            auto&& [pos, elem] = pair;
            return std::array<glm::vec4, 4>{elem.color, elem.color, elem.color, elem.color};
        }));
        auto indices_view   = std::views::join(
            std::views::transform(std::views::enumerate(chunk.iter<Element>()), [](auto&& indexed_pair) {
                auto&& [i, pair]   = indexed_pair;
                std::uint32_t base = static_cast<std::uint32_t>(i) * 4;
                return std::array<std::uint32_t, 6>{{base + 0, base + 1, base + 2, base + 2, base + 3, base + 0}};
            }));

        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions_view)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors_view)
            .with_indices<std::uint32_t>(indices_view);
    }

    static mesh::Mesh build_chunk_outline_mesh(std::size_t chunk_width, float cell_size) {
        float extent = static_cast<float>(chunk_width) * cell_size;

        std::array<glm::vec3, 4> positions{{
            {0.0f, 0.0f, 0.0f},
            {extent, 0.0f, 0.0f},
            {extent, extent, 0.0f},
            {0.0f, extent, 0.0f},
        }};
        std::array<glm::vec4, 4> colors{
            glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
            glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
            glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
            glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
        };
        std::array<std::uint16_t, 8> indices{{0, 1, 1, 2, 2, 3, 3, 0}};

        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eLineList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint16_t>(indices);
    }

    static void build_meshes(Commands cmd,
                             Res<SandSimulation> sim,
                             Query<Item<const grid::Chunk<kDim>&, const SandChunkRenderChildren&>> chunks,
                             ResMut<assets::Assets<mesh::Mesh>> meshes) {
        float cell_size = sim->cell_size();
        std::vector<std::tuple<Entity, std::future<mesh::Mesh>>> mesh_futures;
        for (auto&& [chunk, render_children] : chunks.iter()) {
            const grid::Chunk<kDim>* chunk_ptr = &chunk;
            mesh_futures.emplace_back(render_children.mesh_entity,
                                      sim->thread_pool->submit_task(
                                          [chunk_ptr, cell_size] { return build_chunk_mesh(*chunk_ptr, cell_size); }));
        }
        for (auto&& [mesh_entity, mesh_future] : mesh_futures) {
            auto mesh_handle = meshes->emplace(std::move(mesh_future.get()));
            cmd.entity(mesh_entity).insert(mesh::Mesh2d{mesh_handle});
        }
    }

    static void sync_chunk_transforms(Res<SandSimulation> sim,
                                      Query<Item<transform::Transform&, const SandChunkPos&>> chunks) {
        float chunk_world_size = static_cast<float>(sim->chunk_width()) * sim->cell_size();
        for (auto&& [chunk_transform, pos] : chunks.iter()) {
            chunk_transform.translation.x = static_cast<float>(pos.value[0]) * chunk_world_size;
            chunk_transform.translation.y = static_cast<float>(pos.value[1]) * chunk_world_size;
            chunk_transform.translation.z = 0.0f;
        }
    }

    static void sync_chunk_outline_children(Commands cmd,
                                            Res<SandSimulation> sim,
                                            ResMut<assets::Assets<mesh::Mesh>> meshes,
                                            Local<std::optional<float>> prev_cell_size,
                                            Query<Item<Entity, Mut<SandChunkRenderChildren>>> chunks) {
        float current_cell_size = sim->cell_size();
        bool cell_size_changed =
            !prev_cell_size->has_value() || std::abs(prev_cell_size->value() - current_cell_size) > 1e-6f;
        *prev_cell_size = current_cell_size;

        bool show_outline = sim->show_chunk_outlines();
        for (auto&& [chunk_entity, render_children] : chunks.iter()) {
            auto& tracked_children = render_children.get_mut();
            if (cell_size_changed && tracked_children.outline_entity.has_value()) {
                cmd.entity(*tracked_children.outline_entity).despawn();
                tracked_children.outline_entity.reset();
            }
            if (show_outline) {
                if (!tracked_children.outline_entity.has_value()) {
                    auto outline_mesh =
                        meshes->emplace(build_chunk_outline_mesh(sim->chunk_width(), current_cell_size));
                    auto outline_entity             = cmd.entity(chunk_entity)
                                                          .spawn(SandChunkOutline{}, mesh::Mesh2d{outline_mesh},
                                                                 mesh::MeshMaterial2d{
                                                                     .color      = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f),
                                                                     .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                                                                 },
                                                                 transform::Transform{
                                                                     .translation = glm::vec3(0.0f, 0.0f, 0.01f),
                                                                 })
                                                          .id();
                    tracked_children.outline_entity = outline_entity;
                }
            } else {
                if (tracked_children.outline_entity.has_value()) {
                    cmd.entity(*tracked_children.outline_entity).despawn();
                    tracked_children.outline_entity.reset();
                }
            }
        }
    }
};

export struct SimpleFallingSandSettings {
    std::int32_t width      = 128;
    std::int32_t height     = 96;
    std::size_t chunk_shift = 5;
    float cell_size         = 4.0f;
    enum class LayerType { Dense, Sparse, Tree };
    // the layer type to use. packed is not supported, cause it don't have empty cell.
    LayerType layer_type = LayerType::Tree;
};

export struct SimpleFallingSandPlugin {
    SimpleFallingSandSettings settings{};

    void build(core::App& app) {
        auto cfg = settings;

        app.add_systems(
            core::PreStartup,
            core::into([cfg](core::Commands cmd, core::ResMut<assets::Assets<mesh::Mesh>> meshes,
                             core::Query<core::Item<const render::camera::Camera&>> cameras) {
                auto camera_iter = cameras.iter();
                if (camera_iter.begin() == camera_iter.end()) {
                    cmd.spawn(core_graph::core_2d::Camera2DBundle{});
                }

                cmd.insert_resource(SandSimulation(cfg.chunk_shift, cfg.cell_size));

                const std::int32_t chunk_width = static_cast<std::int32_t>(std::size_t(1) << cfg.chunk_shift);
                const std::int32_t chunks_x    = (cfg.width + chunk_width - 1) / chunk_width;
                const std::int32_t chunks_y    = (cfg.height + chunk_width - 1) / chunk_width;

                for (std::int32_t cy = -chunks_y; cy < chunks_y; ++cy) {
                    for (std::int32_t cx = -chunks_x; cx < chunks_x; ++cx) {
                        grid::Chunk<kDim> chunk(cfg.chunk_shift);
                        std::expected<void, grid::ChunkLayerError> layer_res =
                            std::unexpected(grid::ChunkLayerError::LayerMissing);
                        switch (cfg.layer_type) {
                            case SimpleFallingSandSettings::LayerType::Dense:
                                layer_res = chunk.add_layer(
                                    std::make_unique<grid::layers::DenseLayer<kDim, Element>>(cfg.chunk_shift));
                                break;
                            case SimpleFallingSandSettings::LayerType::Sparse:
                                layer_res = chunk.add_layer(
                                    std::make_unique<grid::layers::SparseLayer<kDim, Element>>(cfg.chunk_shift));
                                break;
                            case SimpleFallingSandSettings::LayerType::Tree:
                                layer_res = chunk.add_layer(
                                    std::make_unique<grid::layers::TreeLayer<kDim, Element>>(cfg.chunk_shift));
                                break;
                        }
                        if (!layer_res.has_value()) continue;

                        auto empty_mesh = meshes->emplace(mesh::Mesh{});
                        cmd.spawn(
                               SandChunkPos{{cx, cy}}, std::move(chunk),
                               transform::Transform{
                                   .translation = glm::vec3(static_cast<float>(cx * chunk_width) * cfg.cell_size,
                                                            static_cast<float>(cy * chunk_width) * cfg.cell_size, 0.0f),
                               })
                            .then([empty_mesh = std::move(empty_mesh)](EntityCommands& chunk_entity) mutable {
                                auto mesh_entity = chunk_entity
                                                       .spawn(SandChunkMesh{}, mesh::Mesh2d{empty_mesh},
                                                              mesh::MeshMaterial2d{
                                                                  .color      = glm::vec4(1.0f),
                                                                  .alpha_mode = mesh::MeshAlphaMode2d::Opaque,
                                                              },
                                                              transform::Transform{
                                                                  .translation = glm::vec3(0.0f),
                                                              })
                                                       .id();

                                chunk_entity.insert(SandChunkRenderChildren{
                                    .mesh_entity    = mesh_entity,
                                    .outline_entity = std::nullopt,
                                });
                            });
                    }
                }
            }).set_name("fallingsand setup"));

        app.add_systems(time::FixedUpdate, core::into(SandSimulation::step).set_name("fallingsand step"));
        app.add_systems(time::FixedPostUpdate,
                        core::into(SandSimulation::sync_chunk_transforms, SandSimulation::build_meshes,
                                   SandSimulation::sync_chunk_outline_children)
                            .set_names(std::array{"fallingsand chunk transform sync", "fallingsand mesh sync",
                                                  "fallingsand outline sync"}));
    }
};
}  // namespace epix::ext::fallingsand