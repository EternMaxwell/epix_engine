module;

export module epix.sprite:render;

import :sprite;
import epix.core;

export namespace sprite {
struct SpritePlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace sprite