module;

export module epix.experimental.pixelbody;

export import :structs;
export import :systems;

import epix.core;

namespace epix::experimental::pixelbody {

/**
 * @brief Plugin that wires the Box2D pixel-body bridge into an App.
 *
 * Picks up entities automatically:
 *  - `PixelBodyWorld + Transform`         → a Box2D world is created.
 *  - `PixelBody + Transform + Velocity`   → a b2 body + polygon shapes are built
 *                                           (parent must carry a PixelBodyWorld).
 *  - Chunks under a SandWorld linked via `PixelBodyWorld.sand_world_entity`
 *    receive an auto-managed `SandStaticBody` that mirrors all Solid cells
 *    as Box2D static chain shapes.
 *
 * Does NOT insert any registry resource — it consumes the shared
 * `fs::ElementRegistry` provided by `FallingSandPlugin`.
 */
export struct PixelBodyPlugin {
    void build(epix::core::App& app);
};

}  // namespace epix::experimental::pixelbody
