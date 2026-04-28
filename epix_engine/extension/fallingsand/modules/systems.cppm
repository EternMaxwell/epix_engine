module;
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#endif

export module epix.extension.fallingsand:systems;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.assets;
import epix.mesh;
import epix.transform;
import epix.render;
import epix.time;
import epix.core_graph;
import epix.extension.grid;
import :elements;
import :structs;
import :helpers;

namespace epix::ext::fallingsand {

using namespace epix::core;

// ──────────────────────────────────────────────────────────────────────────────
// System declarations
// ──────────────────────────────────────────────────────────────────────────────

/** @brief Inserts an active SandChunkDirtyRect on every newly spawned chunk entity
 *  (i.e., entities with Chunk<kDim>+SandChunkPos but no SandChunkDirtyRect yet).
 *  Runs before simulate_worlds so the simulation always has a dirty rect to read. */
void setup_chunk_dirty_rects(
    Commands cmd,
    Query<Item<Entity, const Parent&>, Filter<With<grid::Chunk<kDim>, SandChunkPos>, Without<SandChunkDirtyRect>>>
        new_chunks,
    Query<Item<const SandWorld&>> worlds,
    Query<Item<Entity, const SandChunkPos&, Opt<Mut<SandChunkDirtyRect>>, const Parent&>, With<grid::Chunk<kDim>>>
        all_chunks);

/** @brief Creates SandChunkRenderChildren on newly spawned chunk entities.
 *  If the parent world has MeshBuildByPlugin, spawns an empty mesh child entity.
 *  Also always prepares for outline management (outline_entity starts as nullopt). */
void setup_chunk_render_children(
    Commands cmd,
    ResMut<assets::Assets<mesh::Mesh>> meshes,
    Query<Item<Entity, const Parent&>, Filter<With<grid::Chunk<kDim>, SandChunkPos>, Without<SandChunkRenderChildren>>>
        new_chunks,
    Query<Entity, With<SandWorld, transform::Transform, MeshBuildByPlugin>> mesh_worlds);

/** @brief Simulation step: for each unpaused world with SimulatedByPlugin, assembles a
 *  transient SandSimulation from child chunks (those with SandChunkDirtyRect) and calls step().
 *  The factory reads dirty rects to determine which chunks to simulate; after stepping,
 *  dirty rects are reset and re-marked based on freefall elements. */
void simulate_worlds(
    Res<ElementRegistry> registry,
    Query<Item<Entity, Mut<SandWorld>, const transform::Transform&, Opt<const Children&>>, With<SimulatedByPlugin>>
        worlds,
    Query<Item<Mut<grid::Chunk<kDim>>, const SandChunkPos&, Mut<SandChunkDirtyRect>, const Parent&>> all_chunks);

/** @brief Syncs the world-space Transform of each chunk entity to match its SandChunkPos. */
void sync_chunk_transforms(Query<Item<Entity, transform::Transform&, const SandChunkPos&, const Parent&>> chunks,
                           Query<Item<const SandWorld&>, With<SandWorld>> worlds);

/** @brief Builds/updates chunk content meshes for worlds with MeshBuildByPlugin.
 *  Only rebuilds chunks whose SandChunkDirtyRect is active; clears the rect after rebuild. */
void build_chunk_meshes(
    Commands cmd,
    Query<
        Item<Entity, const grid::Chunk<kDim>&, const SandChunkRenderChildren&, Mut<SandChunkDirtyRect>, const Parent&>>
        chunks,
    Query<Item<const SandWorld&>, Filter<With<SandWorld, MeshBuildByPlugin>>> worlds,
    ResMut<assets::Assets<mesh::Mesh>> meshes);

/** @brief Manages outline child entities for chunks of any SandWorld+Transform entity. */
void update_chunk_outlines(Commands cmd,
                           Query<Item<Entity, Mut<SandChunkRenderChildren>, const Parent&>> chunks,
                           Query<Item<const SandWorld&>, With<SandWorld, transform::Transform>> worlds,
                           ResMut<assets::Assets<mesh::Mesh>> meshes);

}  // namespace epix::ext::fallingsand
