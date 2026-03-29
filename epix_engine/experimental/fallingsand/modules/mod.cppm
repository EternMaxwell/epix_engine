export module epix.experimental.fallingsand;

import std;
import glm;
import BS.thread_pool;

import epix.assets;
import epix.core;
import epix.core_graph;
import epix.mesh;
import epix.render;
import epix.transform;
import epix.input;
import epix.window;

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

export struct SandSimulation : grid::ExtendibleChunkRefGrid<kDim> {
   private:
    std::unique_ptr<ElementRegistry> element_registry;
    std::unique_ptr<BS::thread_pool<>> thread_pool;
    std::size_t m_sand_base_id  = 0;
    float m_cell_size           = 6.0f;
    bool m_initialized          = false;
    bool m_paused               = false;
    bool m_auto_fall            = true;
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

        auto chunk_coords = iter_chunk_pos() | std::ranges::to<std::vector>();
        static auto rng   = std::mt19937{std::random_device{}()};
        std::shuffle(chunk_coords.begin(), chunk_coords.end(), rng);

        for (auto&& [rx, ry] : std::views::cartesian_product(std::views::iota(0, 3), std::views::iota(0, 3))) {
            for (auto&& cpos : chunk_coords | std::views::filter([rx, ry](auto&& cpos) {
                                   auto&& [cx, cy] = cpos;
                                   auto x_r        = (cx % 3 + 3) % 3;
                                   auto y_r        = (cy % 3 + 3) % 3;
                                   return x_r == rx && y_r == ry;
                               })) {
                thread_pool->detach_task([cpos, cw, this] {
                    for (auto&& [x, y] : get_chunk(cpos).value().get().iter<Element>() | std::views::elements<0> |
                                             std::views::transform([cpos, cw](auto&& cell_pos) {
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
                    float t = static_cast<float>(std::hash<std::uint64_t>{}(sd)) /
                              static_cast<float>(std::numeric_limits<std::uint64_t>::max());
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
        auto positions_view = chunk.iter<Element>() | std::views::elements<0> |
                              std::views::transform([cell_size](std::array<std::uint32_t, kDim> pos) {
                                  float x = static_cast<float>(pos[0]) * cell_size;
                                  float y = static_cast<float>(pos[1]) * cell_size;
                                  return std::array<glm::vec3, 4>{{{x, y, 0.0f},
                                                                   {x + cell_size, y, 0.0f},
                                                                   {x + cell_size, y + cell_size, 0.0f},
                                                                   {x, y + cell_size, 0.0f}}};
                              }) |
                              std::views::join;
        auto colors_view    = chunk.iter<Element>() | std::views::transform([&chunk](auto&& pair) {
                               auto&& [pos, elem] = pair;
                               return std::array<glm::vec4, 4>{elem.color, elem.color, elem.color, elem.color};
                              }) |
                              std::views::join;
        auto indices_view =
            chunk.iter<Element>() | std::views::enumerate | std::views::transform([](auto&& indexed_pair) {
                auto&& [i, pair]   = indexed_pair;
                std::uint32_t base = static_cast<std::uint32_t>(i) * 4;
                return std::array<std::uint32_t, 6>{{base + 0, base + 1, base + 2, base + 2, base + 3, base + 0}};
            }) |
            std::views::join;

        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions_view)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors_view)
            .with_indices<std::uint32_t>(indices_view);
    }

    static void build_meshes(Commands cmd,
                             Res<SandSimulation> sim,
                             Query<Item<Entity, const SandChunkPos&, const grid::Chunk<kDim>&>> chunks,
                             ResMut<assets::Assets<mesh::Mesh>> meshes) {
        float cell_size = sim->cell_size();
        std::vector<std::tuple<Entity, std::array<std::int32_t, kDim>, std::future<mesh::Mesh>>> mesh_futures;
        for (auto&& [entity, pos, chunk] : chunks.iter()) {
            mesh_futures.emplace_back(entity, pos.value, sim->thread_pool->submit_task([&chunk, cell_size] {
                return build_chunk_mesh(chunk, cell_size);
            }));
        }
        for (auto&& [entity, pos, mesh_future] : mesh_futures) {
            auto mesh_handle = meshes->emplace(std::move(mesh_future.get()));
            cmd.entity(entity).insert(mesh::Mesh2d{mesh_handle},
                                      mesh::MeshMaterial2d{
                                          .color      = glm::vec4(1.0f),
                                          .alpha_mode = mesh::MeshAlphaMode2d::Opaque,
                                      },
                                      transform::Transform{
                                          .translation = glm::vec3(pos[0] * cell_size * sim->chunk_width(),
                                                                   pos[1] * cell_size * sim->chunk_width(), 0.0f),
                                      });
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
                            SandChunkPos{{cx, cy}}, std::move(chunk), mesh::Mesh2d{empty_mesh},
                            mesh::MeshMaterial2d{
                                .color      = glm::vec4(1.0f),
                                .alpha_mode = mesh::MeshAlphaMode2d::Opaque,
                            },
                            transform::Transform{
                                .translation = glm::vec3(static_cast<float>(cx * chunk_width) * cfg.cell_size,
                                                         static_cast<float>(cy * chunk_width) * cfg.cell_size, 0.0f),
                            });
                    }
                }
            }).set_name("fallingsand setup"));

        app.add_systems(core::Update, core::into(SandSimulation::step).set_name("fallingsand step"));
        app.add_systems(core::PostUpdate, core::into(SandSimulation::build_meshes).set_name("fallingsand mesh sync"));
    }
};
}  // namespace ext::fallingsand