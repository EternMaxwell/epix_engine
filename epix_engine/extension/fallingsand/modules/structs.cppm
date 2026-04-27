module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <variant>
#endif

export module epix.extension.fallingsand:structs;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import epix.core;
import epix.extension.grid;
import :elements;

namespace epix::ext::fallingsand {

/** @brief Chunk coordinate component — placed on child entities of a SandWorld entity. */
export struct SandChunkPos {
    std::array<std::int32_t, kDim> value;
};

/** @brief Marker: entity is simulated automatically by FallingSandPlugin. */
export struct SimulatedByPlugin {};

/** @brief Marker: entity's chunk content meshes are built by FallingSandPlugin. */
export struct MeshBuildByPlugin {};

/** @brief Tag for the chunk content mesh child entity. */
export struct SandChunkMesh {};

/** @brief Tag for the chunk outline mesh child entity. */
export struct SandChunkOutline {};

/**
 * @brief Tracks render-child entities for a chunk entity.
 *
 * `mesh_entity`    — present when the parent world has MeshBuildByPlugin.
 * `outline_entity` — present when show_chunk_outlines is true.
 */
export struct SandChunkRenderChildren {
    std::optional<core::Entity> mesh_entity;
    std::optional<core::Entity> outline_entity;
    float outline_cell_size = 0.0f;  ///< Cell size used when outline_entity was built.
};

/**
 * @brief Pure-configuration component for a falling-sand simulation world entity.
 *
 * Stores only settings (chunk layout, cell size, paused state, tick counter).
 * The element registry and thread pool live elsewhere — pass them explicitly to
 * SandSimulation::create at step time.
 */
export struct SandWorld {
   private:
    std::size_t m_chunk_shift  = 5;
    float m_cell_size          = 4.0f;
    bool m_paused              = false;
    bool m_show_chunk_outlines = true;
    glm::vec2 m_gravity        = {0.0f, -300.0f};  ///< Gravity acceleration in cells/s².

   public:
    SandWorld() = default;
    SandWorld(std::size_t chunk_shift, float cell_size) : m_chunk_shift(chunk_shift), m_cell_size(cell_size) {}

    std::size_t chunk_shift() const { return m_chunk_shift; }
    float cell_size() const { return m_cell_size; }
    void set_cell_size(float s) { m_cell_size = s; }
    bool paused() const { return m_paused; }
    void set_paused(bool p) { m_paused = p; }
    bool show_chunk_outlines() const { return m_show_chunk_outlines; }
    void set_show_chunk_outlines(bool s) { m_show_chunk_outlines = s; }
    glm::vec2 gravity() const { return m_gravity; }
    void set_gravity(glm::vec2 g) { m_gravity = g; }
};

/**
 * @brief Per-chunk dirty-rectangle that tracks the active simulation area.
 *
 * Place this component on chunk entities alongside SandChunkPos.
 * Call `touch(x, y)` (chunk-local coordinates) whenever a cell moves or is disturbed
 * so that subsequent ticks can skip fully idle chunks.
 * `active()` returns false when the rectangle is in the reset/empty state.
 */
export struct SandChunkDirtyRect {
    std::int32_t xmin = std::numeric_limits<std::int32_t>::max();
    std::int32_t xmax = std::numeric_limits<std::int32_t>::min();
    std::int32_t ymin = std::numeric_limits<std::int32_t>::max();
    std::int32_t ymax = std::numeric_limits<std::int32_t>::min();

    bool active() const noexcept { return xmin <= xmax && ymin <= ymax; }

    void touch(std::int32_t x, std::int32_t y) noexcept {
        xmin = std::min(xmin, x);
        xmax = std::max(xmax, x);
        ymin = std::min(ymin, y);
        ymax = std::max(ymax, y);
    }

    void expand(std::int32_t margin) noexcept {
        xmin -= margin;
        xmax += margin;
        ymin -= margin;
        ymax += margin;
    }

    void reset() noexcept {
        xmin = std::numeric_limits<std::int32_t>::max();
        xmax = std::numeric_limits<std::int32_t>::min();
        ymin = std::numeric_limits<std::int32_t>::max();
        ymax = std::numeric_limits<std::int32_t>::min();
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — transient wrapper for a single simulation step.
// ──────────────────────────────────────────────────────────────────────────────

/** @brief Error variants returned by SandSimulation::create. */
export namespace sand_sim_error {
/** @brief Two chunks share the same SandChunkPos under the same world. */
struct DuplicateChunkPos {
    std::array<std::int32_t, kDim> pos;
};
}  // namespace sand_sim_error

/** @brief Error type returned by SandSimulation::create. */
export using SandSimCreateError = std::variant<sand_sim_error::DuplicateChunkPos, grid::ChunkGridError>;

/**
 * @brief Transient simulation handle assembled from a SandWorld and its chunk children.
 *
 * Construct at step time from:
 *   - a mutable SandWorld reference (provides settings and registry), and
 *   - a range whose elements are `std::tuple<grid::Chunk<kDim>&, const SandChunkPos&>`.
 *
 * Example using iter-children-with-filter:
 * @code
 *   auto range = std::views::transform(
 *       std::views::filter(children.entities(),
 *           [&cq](Entity e) { return cq.get(e).has_value(); }),
 *       [&cq](Entity e) -> std::tuple<grid::Chunk<kDim>&, const SandChunkPos&> {
 *           auto&& [c, p] = *cq.get(e);
 *           return {c, p};
 *       });
 *   SandSimulation sim(world, range);
 * @endcode
 */
export struct SandSimulation : grid::ExtendibleChunkRefGrid<kDim> {
   private:
    SandWorld* m_world;
    const ElementRegistry* m_registry;

    explicit SandSimulation(SandWorld& world, const ElementRegistry& registry)
        : grid::ExtendibleChunkRefGrid<kDim>(world.chunk_shift()), m_world(&world), m_registry(&registry) {}

    enum class CellState { Occupied, EmptyInChunk, Blocked };
    CellState cell_state(std::int64_t x, std::int64_t y) const;
    bool has_cell(std::int64_t x, std::int64_t y) const;
    bool set_cell(std::int64_t x, std::int64_t y, Element value);
    bool clear_cell(std::int64_t x, std::int64_t y);
    bool move_cell(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty);
    bool swap_cells(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty);
    void mutate_cell(std::int64_t x, std::int64_t y, epix::utils::function_ref<void(Element&)> fn);

    /// @brief Raycast step result.
    struct RaycastResult {
        int steps;
        std::int64_t new_x, new_y;
        std::optional<std::pair<std::int64_t, std::int64_t>> hit;
    };

    Element* get_elem_ptr(std::int64_t x, std::int64_t y);
    bool valid(std::int64_t x, std::int64_t y) const;
    RaycastResult raycast_to(std::int64_t x, std::int64_t y, std::int64_t tx, std::int64_t ty);
    bool collide(std::int64_t x, std::int64_t y, std::int64_t tx, std::int64_t ty);
    void touch(std::int64_t x, std::int64_t y);
    glm::vec2 get_grav(std::int64_t x, std::int64_t y) const;
    glm::vec2 get_default_vel(std::int64_t x, std::int64_t y) const;
    float air_density(std::int64_t x, std::int64_t y) const;
    int not_moving_threshold(glm::vec2 grav) const;

    void step_particle_powder(std::int64_t x, std::int64_t y, std::uint64_t tick, float delta);
    void step_particle_liquid(std::int64_t x, std::int64_t y, std::uint64_t tick, float delta);
    void step_particle_gas(std::int64_t x, std::int64_t y, std::uint64_t tick, float delta);

    void step_particle(std::int64_t x, std::int64_t y, std::uint64_t tick);
    void step_cells();

   public:
    /**
     * @brief Factory: build a SandSimulation from a SandWorld and a range of
     *        `(Chunk<kDim>&, SandChunkPos)` pairs.
     *
     * Conflict detection is performed automatically; on duplicate chunk position the
     * factory returns `std::unexpected(sand_sim_error::DuplicateChunkPos{pos})`.
     * Any other grid insert failure is returned as `std::unexpected(ChunkGridError{...})`.
     *
     * Example (building from children query in a system):
     * @code
     *   auto range = children
     *       | std::views::filter([&cq](Entity e) { return cq.get(e).has_value(); })
     *       | std::views::transform([&cq](Entity e)
     *             -> std::tuple<grid::Chunk<kDim>&, const SandChunkPos&> {
     *           auto&& [c, p, _] = *cq.get(e); return {c.get_mut(), p};
     *         });
     *   auto sim = SandSimulation::create(world, range);
     * @endcode
     */
    template <std::ranges::input_range R>
    static std::expected<SandSimulation, SandSimCreateError> create(SandWorld& world,
                                                                    const ElementRegistry& registry,
                                                                    R&& chunks) {
        SandSimulation sim(world, registry);
        for (auto&& item : std::forward<R>(chunks)) {
            auto&& [chunk, pos] = item;
            auto result         = sim.insert_chunk(pos.value, chunk);
            if (!result.has_value()) {
                auto& err = result.error();
                if (std::holds_alternative<grid::grid_error>(err) &&
                    std::get<grid::grid_error>(err) == grid::grid_error::AlreadyOccupied) {
                    return std::unexpected(SandSimCreateError{sand_sim_error::DuplicateChunkPos{pos.value}});
                }
                return std::unexpected(SandSimCreateError{err});
            }
        }
        return sim;
    }

    SandWorld& world_mut() { return *m_world; }
    const SandWorld& world() const { return *m_world; }

    /** @brief Run one simulation step. */
    void step();
};

}  // namespace epix::ext::fallingsand