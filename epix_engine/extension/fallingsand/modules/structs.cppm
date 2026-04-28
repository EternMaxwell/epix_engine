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
    std::size_t m_chunk_shift     = 5;
    float m_cell_size             = 4.0f;
    bool m_paused                 = false;
    bool m_show_chunk_outlines    = true;
    bool m_missing_chunk_as_solid = false;            ///< Treat absent chunks as solid walls.
    glm::vec2 m_gravity           = {0.0f, -300.0f};  ///< Gravity acceleration in cells/s².

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
    bool missing_chunk_as_solid() const { return m_missing_chunk_as_solid; }
    void set_missing_chunk_as_solid(bool v) { m_missing_chunk_as_solid = v; }
    glm::vec2 gravity() const { return m_gravity; }
    void set_gravity(glm::vec2 g) { m_gravity = g; }
};

/**
 * @brief Per-chunk dirty-rectangle that tracks the active simulation area.
 *
 * Two-buffer system matching old feature/pixel_b2d:
 *   - Current buffer (xmin/xmax/ymin/ymax): active area this frame.
 *   - Next buffer (xmin_next/xmax_next/ymin_next/ymax_next): accumulates touches this frame.
 *   - count_time() is called each tick; when time_threshold is reached it swaps
 *     next → current and resets next.  Returns true (settled) when the chunk
 *     has no active area after the swap — caller should force-sleep all cells.
 *   - touch(x, y): updates both buffers.  If current is not active it is first
 *     expanded to the full chunk so the step loop covers everything.
 */
export struct SandChunkDirtyRect {
    // Current active area (empty = xmin > xmax)
    std::int32_t xmin = 0;
    std::int32_t xmax = 0;
    std::int32_t ymin = 0;
    std::int32_t ymax = 0;
    // Next-frame accumulated area
    std::int32_t xmin_next = 0;
    std::int32_t xmax_next = 0;
    std::int32_t ymin_next = 0;
    std::int32_t ymax_next = 0;
    // Timer
    std::int32_t time_since_last_swap = 0;
    std::int32_t time_threshold       = 12;
    // Chunk side length in cells (needed to represent "empty" as xmin=width, xmax=0)
    std::int32_t width = 0;
    // Set to true when chunk just settled (force-slept); triggers one final mesh rebuild.
    bool needs_rebuild = false;

    /** Initialize in the "not active" / empty state for a chunk of the given width. */
    static SandChunkDirtyRect make_empty(std::int32_t chunk_width) noexcept {
        SandChunkDirtyRect r;
        r.width     = chunk_width;
        r.xmin      = chunk_width;
        r.xmax      = 0;
        r.ymin      = chunk_width;
        r.ymax      = 0;
        r.xmin_next = chunk_width;
        r.xmax_next = 0;
        r.ymin_next = chunk_width;
        r.ymax_next = 0;
        return r;
    }

    /** Initialize in the "full chunk active" state. */
    static SandChunkDirtyRect make_full(std::int32_t chunk_width) noexcept {
        SandChunkDirtyRect r;
        r.width     = chunk_width;
        r.xmin      = 0;
        r.xmax      = chunk_width - 1;
        r.ymin      = 0;
        r.ymax      = chunk_width - 1;
        r.xmin_next = 0;
        r.xmax_next = chunk_width - 1;
        r.ymin_next = 0;
        r.ymax_next = chunk_width - 1;
        return r;
    }

    bool active() const noexcept { return xmin <= xmax && ymin <= ymax; }

    // touch: update both current and next buffers (matches old Chunk::touch)
    void touch(std::int32_t x, std::int32_t y) noexcept {
        if (!active()) {
            xmin = 0;
            xmax = width - 1;
            ymin = 0;
            ymax = width - 1;
        }
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
        if (x < xmin_next) xmin_next = x;
        if (x > xmax_next) xmax_next = x;
        if (y < ymin_next) ymin_next = y;
        if (y > ymax_next) ymax_next = y;
    }

    // swap_area: copy next → current, reset next.
    // Returns true when chunk has no active area after swap (settled).
    bool swap_area() noexcept {
        xmin      = xmin_next;
        xmax      = xmax_next;
        ymin      = ymin_next;
        ymax      = ymax_next;
        xmin_next = width;
        xmax_next = 0;
        ymin_next = width;
        ymax_next = 0;
        return !active();
    }

    // count_time: increment timer; swap when threshold reached.
    // Returns true when chunk settled (caller should force-sleep all cells).
    bool count_time() noexcept {
        time_since_last_swap++;
        bool settled = false;
        if (time_since_last_swap >= time_threshold) {
            time_since_last_swap = 0;
            settled              = swap_area();
        }
        time_threshold = 12;
        return settled;
    }

    // in_area: check next-frame buffer (matches old Chunk::in_area)
    bool in_area(std::int32_t x, std::int32_t y) const noexcept {
        return x >= xmin_next && x <= xmax_next && y >= ymin_next && y <= ymax_next;
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
    grid::tree_extendible_grid<kDim, SandChunkDirtyRect*> m_chunk_dirty_rects;

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
    /** @brief Mark cell (x,y) as active: expand its chunk's dirty rect and wake the cell. */
    void touch(std::int64_t x, std::int64_t y);
    /**
     * @brief Factory: build a SandSimulation from a SandWorld and a range of
     *        `(Chunk<kDim>&, SandChunkPos, SandChunkDirtyRect&)` triples.
     *
     * Only chunks whose SandChunkDirtyRect is active are scheduled for stepping
     * (stored in m_active_chunks).  Pass an active rect to guarantee a chunk is
     * stepped on the first frame after spawn.
     *
     * Conflict detection is performed automatically; on duplicate chunk position the
     * factory returns `std::unexpected(sand_sim_error::DuplicateChunkPos{pos})`.
     * Any other grid insert failure is returned as `std::unexpected(ChunkGridError{...})`.
     */
    template <std::ranges::input_range R>
        requires std::same_as<std::tuple<grid::Chunk<kDim>&, const SandChunkPos&, SandChunkDirtyRect&>,
                              std::ranges::range_value_t<R>>
    static std::expected<SandSimulation, SandSimCreateError> create(SandWorld& world,
                                                                    const ElementRegistry& registry,
                                                                    R&& chunks) {
        SandSimulation sim(world, registry);
        for (auto&& item : std::forward<R>(chunks)) {
            auto&& [chunk, pos, dirty_rect] = item;
            auto result                     = sim.insert_chunk(pos.value, chunk);
            if (!result.has_value()) {
                auto& err = result.error();
                if (std::holds_alternative<grid::grid_error>(err) &&
                    std::get<grid::grid_error>(err) == grid::grid_error::AlreadyOccupied) {
                    return std::unexpected(SandSimCreateError{sand_sim_error::DuplicateChunkPos{pos.value}});
                }
                return std::unexpected(SandSimCreateError{err});
            }
            (void)sim.m_chunk_dirty_rects.set(pos.value, &dirty_rect);
        }
        return sim;
    }

    SandWorld& world_mut() { return *m_world; }
    const SandWorld& world() const { return *m_world; }

    /** @brief Run one simulation step. */
    void step();
};

}  // namespace epix::ext::fallingsand