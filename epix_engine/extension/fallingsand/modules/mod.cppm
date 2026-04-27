module;

export module epix.extension.fallingsand;

export import :elements;
export import :structs;
export import :helpers;
export import :systems;

import epix.core;

namespace epix::ext::fallingsand {

/**
 * @brief Plugin that automates simulation and rendering of SandWorld entities.
 *
 * Entities are picked up automatically based on their components:
 *  - `SandWorld + Transform + SimulatedByPlugin`  → simulated each FixedUpdate.
 *  - `SandWorld + Transform + MeshBuildByPlugin`  → chunk content meshes are built.
 *  - `SandWorld + Transform` (+ show_chunk_outlines) → chunk outlines are managed.
 *
 * Child entities with `grid::Chunk<2> + SandChunkPos` are treated as chunks.
 * Conflicting chunk positions (two chunks at the same SandChunkPos) are logged as errors
 * and the affected world is skipped for that tick.
 */
export struct FallingSandPlugin {
    void build(epix::core::App& app);
};

}  // namespace epix::ext::fallingsand
