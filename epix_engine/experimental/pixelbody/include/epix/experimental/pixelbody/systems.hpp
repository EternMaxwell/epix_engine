#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/extension/fallingsand.hpp>
#include <epix/extension/grid.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#endif

#include <epix/experimental/pixelbody/structs.hpp>

namespace epix::experimental::pixelbody {

namespace fs   = epix::ext::fallingsand;
namespace grid = epix::ext::grid;

// ─────────────────────────────────────────────────────────────────────────────
// System declarations
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Create a Box2D world for any new PixelBodyWorld with a null id. */
void init_pixel_body_worlds(core::Query<core::Item<core::Mut<PixelBodyWorld>>> worlds);

/** @brief For each PixelBody with a null b2 body id, build the b2 body + shapes
 *  from its cell grid (outline + earcut + b2MakePolygon).  Looks up the parent
 *  PixelBodyWorld via the Parent component. */
void init_pixel_bodies(
    core::Query<core::Item<core::Entity, core::Mut<PixelBody>, const transform::Transform&, const core::Parent&>>
        bodies,
    core::Query<core::Item<core::Mut<PixelBodyWorld>>> worlds,
    core::Res<fs::ElementRegistry> registry);

/** @brief Rebuild b2 polygon shapes for any existing PixelBody with shapes_dirty. */
void rebuild_pixel_body_shapes(core::Query<core::Item<core::Mut<PixelBody>, const core::Parent&>> bodies,
                               core::Query<core::Item<const PixelBodyWorld&>> worlds,
                               core::Res<fs::ElementRegistry> registry);

/** @brief Push ECS Transform/Velocity into the b2 body each frame. */
void sync_transforms_to_b2(
    core::Query<core::Item<const PixelBody&, const transform::Transform&, const Velocity&>> bodies);

/** @brief Walk every chunk of every PixelBodyWorld+SandWorld entity and (re)build
 *  per-chunk static b2 bodies of stone (Solid-typed cells). */
void update_sand_static_bodies(
    core::Commands cmd,
    core::Res<fs::ElementRegistry> registry,
    core::Query<core::Item<core::Entity, const PixelBodyWorld&, const fs::SandWorld&, core::Opt<const core::Children&>>>
        worlds,
    core::Query<core::Item<core::Entity,
                           core::Ref<fs::ChunkElementGrid>,
                           const fs::SandChunkPos&,
                           core::Opt<core::Mut<fs::SandChunkDirtyRect>>,
                           core::Opt<core::Mut<SandStaticBody>>>> chunks);

/** @brief Step the Box2D world via fixed-timestep accumulator. */
void step_pixel_body_worlds(core::Res<time::Time<>> time, core::Query<core::Item<core::Mut<PixelBodyWorld>>> worlds);

/** @brief Pull the new b2 transform back into the ECS Transform/Velocity. */
void sync_b2_to_transforms(
    core::Query<core::Item<const PixelBody&, core::Mut<transform::Transform>, core::Mut<Velocity>>> bodies);

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
void sync_pixel_body_to_sand(core::Commands cmd,
                             core::ResMut<fs::ElementRegistry> registry,
                             core::Query<core::Item<core::Entity,
                                                    const PixelBodyWorld&,
                                                    core::Mut<fs::SandWorld>,
                                                    core::Opt<const core::Children&>,
                                                    core::Opt<core::Mut<PixelBodySandBlockers>>>> worlds,
                             core::Query<core::Item<const PixelBody&, const transform::Transform&>> bodies,
                             core::Query<core::Item<core::Mut<fs::ChunkElementGrid>,
                                                    core::Mut<fs::ChunkAirGrid>,
                                                    core::Mut<fs::ChunkThermalGrid>,
                                                    const fs::SandChunkPos&,
                                                    core::Mut<fs::SandChunkDirtyRect>>> chunks);

/** @brief Rebuild the render mesh for any PixelBody with mesh_dirty. */
void build_pixel_body_meshes(core::Commands cmd,
                             core::ResMut<assets::Assets<mesh::Mesh>> meshes,
                             core::Query<core::Item<core::Entity, core::Mut<PixelBody>, const core::Parent&>> bodies,
                             core::Query<core::Item<const PixelBodyWorld&>> worlds);

}  // namespace epix::experimental::pixelbody
