#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
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
void init_pixel_body_worlds(ecs::Query<ecs::Item<ecs::Mut<PixelBodyWorld>>> worlds);

/** @brief For each PixelBody with a null b2 body id, build the b2 body + shapes
 *  from its cell grid (outline + earcut + b2MakePolygon).  Looks up the parent
 *  PixelBodyWorld via the Parent component. */
void init_pixel_bodies(
    ecs::Query<ecs::Item<ecs::Entity, ecs::Mut<PixelBody>, const transform::Transform&, const ecs::Parent&>> bodies,
    ecs::Query<ecs::Item<ecs::Mut<PixelBodyWorld>>> worlds,
    ecs::Res<fs::ElementRegistry> registry);

/** @brief Rebuild b2 polygon shapes for any existing PixelBody with shapes_dirty. */
void rebuild_pixel_body_shapes(ecs::Query<ecs::Item<ecs::Mut<PixelBody>, const ecs::Parent&>> bodies,
                               ecs::Query<ecs::Item<const PixelBodyWorld&>> worlds,
                               ecs::Res<fs::ElementRegistry> registry);

/** @brief Push ECS Transform/Velocity into the b2 body each frame. */
void sync_transforms_to_b2(
    ecs::Query<ecs::Item<const PixelBody&, const transform::Transform&, const Velocity&>> bodies);

/** @brief Walk every chunk of every PixelBodyWorld+SandWorld entity and (re)build
 *  per-chunk static b2 bodies of stone (Solid-typed cells). */
void update_sand_static_bodies(
    ecs::Commands cmd,
    ecs::Res<fs::ElementRegistry> registry,
    ecs::Query<ecs::Item<ecs::Entity, const PixelBodyWorld&, const fs::SandWorld&, ecs::Opt<const ecs::Children&>>>
        worlds,
    ecs::Query<ecs::Item<ecs::Entity,
                         ecs::Ref<fs::ChunkElementGrid>,
                         const fs::SandChunkPos&,
                         ecs::Opt<ecs::Mut<fs::SandChunkDirtyRect>>,
                         ecs::Opt<ecs::Mut<SandStaticBody>>>> chunks);

/** @brief Step the Box2D world via fixed-timestep accumulator. */
void step_pixel_body_worlds(ecs::Res<time::Time<>> time, ecs::Query<ecs::Item<ecs::Mut<PixelBodyWorld>>> worlds);

/** @brief Pull the new b2 transform back into the ECS Transform/Velocity. */
void sync_b2_to_transforms(
    ecs::Query<ecs::Item<const PixelBody&, ecs::Mut<transform::Transform>, ecs::Mut<Velocity>>> bodies);

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
void sync_pixel_body_to_sand(ecs::Commands cmd,
                             ecs::ResMut<fs::ElementRegistry> registry,
                             ecs::Query<ecs::Item<ecs::Entity,
                                                  const PixelBodyWorld&,
                                                  ecs::Mut<fs::SandWorld>,
                                                  ecs::Opt<const ecs::Children&>,
                                                  ecs::Opt<ecs::Mut<PixelBodySandBlockers>>>> worlds,
                             ecs::Query<ecs::Item<const PixelBody&, const transform::Transform&>> bodies,
                             ecs::Query<ecs::Item<ecs::Mut<fs::ChunkElementGrid>,
                                                  ecs::Mut<fs::ChunkAirGrid>,
                                                  ecs::Mut<fs::ChunkThermalGrid>,
                                                  const fs::SandChunkPos&,
                                                  ecs::Mut<fs::SandChunkDirtyRect>>> chunks);

/** @brief Rebuild the render mesh for any PixelBody with mesh_dirty. */
void build_pixel_body_meshes(ecs::Commands cmd,
                             ecs::ResMut<assets::Assets<mesh::Mesh>> meshes,
                             ecs::Query<ecs::Item<ecs::Entity, ecs::Mut<PixelBody>, const ecs::Parent&>> bodies,
                             ecs::Query<ecs::Item<const PixelBodyWorld&>> worlds);

}  // namespace epix::experimental::pixelbody
