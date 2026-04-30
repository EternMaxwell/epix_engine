module;
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#endif

#include <box2d/box2d.h>
#include <box2d/id.h>
#include <box2d/types.h>

export module epix.experimental.pixelbody:structs;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import epix.core;
import epix.extension.grid;
import epix.extension.fallingsand;

namespace epix::experimental::pixelbody {

namespace fs   = epix::ext::fallingsand;
namespace grid = epix::ext::grid;

// ─────────────────────────────────────────────────────────────────────────────
// PixelBodyWorld — component on the parent entity that owns a Box2D world.
// Lives alongside a Transform; child entities (with PixelBody) are simulated
// inside this world.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Per-world settings + handle to the Box2D world.
 *
 * Coordinate convention: 1 b2 unit == 1 pixel.  The Box2D gravity vector is
 * computed as `gravity_cells * cell_size` so a sand cell falling 1 cell/s² is
 * equivalent to a body falling `cell_size` pixels/s² in Box2D space.
 *
 * To bridge a Box2D world with a falling-sand simulation, place the
 * `PixelBodyWorld` component on the SAME ENTITY as the `fs::SandWorld`.
 * Sand chunks (children of that entity) become per-chunk static bodies in this
 * b2 world, and dynamic `PixelBody` children of the same entity push sand
 * cells out of the way during interaction.
 */
export struct PixelBodyWorld {
    b2WorldId b2_world      = b2_nullWorldId;
    float cell_size         = 4.0f;
    glm::vec2 gravity_cells = {0.0f, -300.0f};
    float fixed_dt          = 1.0f / 60.0f;
    int sub_steps           = 4;
    float accumulator       = 0.0f;
    bool paused             = false;

    glm::vec2 b2_gravity() const noexcept { return gravity_cells * cell_size; }
};

// ─────────────────────────────────────────────────────────────────────────────
// PixelBody — component on a body entity (child of a PixelBodyWorld entity).
// Stores its cell grid as a dense_grid<2, fs::Element> and tracks the b2 body
// + shape ids for rebuild/cleanup.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A rigid body composed of a dense grid of falling-sand cells.
 *
 * The body's transform is in world pixel space (matches Box2D coords).  Cells
 * are stored relative to the body's local origin in `cells`.  `base_id` per
 * cell indexes the shared `fs::ElementRegistry` resource.
 *
 * Dirty flags drive lazy rebuilds:
 *   - `shapes_dirty`   → b2 polygon shapes (re-triangulate via earcut + b2MakePolygon)
 *   - `mesh_dirty`     → render mesh asset
 *   - `physics_dirty`  → mass/density properties (recomputed in shape rebuild)
 */
export struct PixelBody {
    b2BodyId b2_body = b2_nullBodyId;
    std::vector<b2ShapeId> shapes;
    grid::dense_grid<2, fs::Element> cells{{1u, 1u}};
    bool is_dynamic    = true;
    bool shapes_dirty  = true;
    bool mesh_dirty    = true;
    bool physics_dirty = true;
    /// Cached optional asset handle for the render mesh — populated by
    /// `build_pixel_body_meshes` on the first dirty rebuild.
    std::optional<core::Entity> mesh_child_entity;

    /// Build a uniform rectangular body of size (w × h) filled with the given
    /// element (looked up in the shared `fs::ElementRegistry`).  The colour
    /// of every cell is sampled from `registry[base_id].color_func(seed_offset+i)`
    /// where `i` is a per-cell counter for variety.
    static PixelBody make_solid(std::uint32_t w,
                                std::uint32_t h,
                                std::size_t base_id,
                                const fs::ElementRegistry& registry,
                                std::uint64_t seed = 0);

    /// Construct from an arbitrary pre-filled cell grid (move-in).
    static PixelBody from_grid(grid::dense_grid<2, fs::Element>&& cells);
};

// ─────────────────────────────────────────────────────────────────────────────
// Velocity — basic linear/angular kinematic state synced to/from Box2D.
// ─────────────────────────────────────────────────────────────────────────────

export struct Velocity {
    glm::vec2 linear = {0.0f, 0.0f};
    float angular    = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// SandStaticBody — managed by the pixelbody plugin on each sand chunk entity.
// Holds the per-chunk static b2 body + chain shapes representing all Solid
// cells in the chunk.  `dirty` marks chunks needing rebuild.
// ─────────────────────────────────────────────────────────────────────────────

export struct SandStaticBody {
    b2BodyId b2_body = b2_nullBodyId;
    std::vector<b2ChainId> chains;
    bool dirty = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// PixelBodyOf — relation marker on a body entity pointing back to its world.
// Avoids walking the Parent hierarchy in hot loops.
// ─────────────────────────────────────────────────────────────────────────────

export struct PixelBodyOf {
    core::Entity world;
};

}  // namespace epix::experimental::pixelbody
