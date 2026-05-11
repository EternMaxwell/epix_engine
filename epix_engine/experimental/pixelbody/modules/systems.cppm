module;
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#endif

export module epix.experimental.pixelbody:systems;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.assets;
import epix.mesh;
import epix.transform;
import epix.render;
import epix.time;
import epix.extension.grid;
import epix.extension.fallingsand;
import :structs;

namespace epix::experimental::pixelbody {

using namespace epix::core;
namespace fs   = epix::ext::fallingsand;
namespace grid = epix::ext::grid;

// ─────────────────────────────────────────────────────────────────────────────
// System declarations
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Create a Box2D world for any new PixelBodyWorld with a null id. */
void init_pixel_body_worlds(Query<Item<Mut<PixelBodyWorld>>> worlds);

/** @brief For each PixelBody with a null b2 body id, build the b2 body + shapes
 *  from its cell grid (outline + earcut + b2MakePolygon).  Looks up the parent
 *  PixelBodyWorld via the Parent component. */
void init_pixel_bodies(Query<Item<Entity, Mut<PixelBody>, const transform::Transform&, const Parent&>> bodies,
                       Query<Item<Mut<PixelBodyWorld>>> worlds,
                       Res<fs::ElementRegistry> registry);

/** @brief Rebuild b2 polygon shapes for any existing PixelBody with shapes_dirty. */
void rebuild_pixel_body_shapes(Query<Item<Mut<PixelBody>, const Parent&>> bodies,
                               Query<Item<const PixelBodyWorld&>> worlds,
                               Res<fs::ElementRegistry> registry);

/** @brief Push ECS Transform/Velocity into the b2 body each frame. */
void sync_transforms_to_b2(Query<Item<const PixelBody&, const transform::Transform&, const Velocity&>> bodies);

/** @brief Walk every chunk of every PixelBodyWorld+SandWorld entity and (re)build
 *  per-chunk static b2 bodies of stone (Solid-typed cells). */
void update_sand_static_bodies(
    Commands cmd,
    Res<fs::ElementRegistry> registry,
    Query<Item<Entity, const PixelBodyWorld&, const fs::SandWorld&, Opt<const Children&>>> worlds,
    Query<Item<Entity,
               Ref<fs::ChunkElementGrid>,
               const fs::SandChunkPos&,
               Opt<Mut<fs::SandChunkDirtyRect>>,
               Opt<Mut<SandStaticBody>>>> chunks);

/** @brief Step the Box2D world via fixed-timestep accumulator. */
void step_pixel_body_worlds(Res<time::Time<>> time, Query<Item<Mut<PixelBodyWorld>>> worlds);

/** @brief Pull the new b2 transform back into the ECS Transform/Velocity. */
void sync_b2_to_transforms(Query<Item<const PixelBody&, Mut<transform::Transform>, Mut<Velocity>>> bodies);

/** @brief For each occupied body cell, push the underlying sand cell up the
 *  anti-gravity column until an empty cell is found.  Stone cells block.
 *
 *  Also manages Body-type sentinel elements in the sand grid:
 *  - Removes stale blockers from cells the body vacated (with touch, so sand falls)
 *  - Pushes out any real sand occupying body cells (sand displaced → b2Body woken)
 *  - Inserts new blockers at body cell positions using insert_cell (no dirty-rect
 *    touch) so the sand simulation treats those cells as immovable obstacles
 *    without constantly re-waking settled sand.
 *
 *  Runs in FixedPreUpdate, immediately before the fallingsand simulate step. */
void sync_pixel_body_to_sand(
    Commands cmd,
    ResMut<fs::ElementRegistry> registry,
    Query<
        Item<Entity, const PixelBodyWorld&, Mut<fs::SandWorld>, Opt<const Children&>, Opt<Mut<PixelBodySandBlockers>>>>
        worlds,
    Query<Item<const PixelBody&, const transform::Transform&>> bodies,
    Query<Item<Mut<fs::ChunkElementGrid>,
               Mut<fs::ChunkAirGrid>,
               Mut<fs::ChunkThermalGrid>,
               const fs::SandChunkPos&,
               Mut<fs::SandChunkDirtyRect>>> chunks);

/** @brief Rebuild the render mesh for any PixelBody with mesh_dirty. */
void build_pixel_body_meshes(Commands cmd,
                             ResMut<assets::Assets<mesh::Mesh>> meshes,
                             Query<Item<Entity, Mut<PixelBody>, const Parent&>> bodies,
                             Query<Item<const PixelBodyWorld&>> worlds);

}  // namespace epix::experimental::pixelbody
