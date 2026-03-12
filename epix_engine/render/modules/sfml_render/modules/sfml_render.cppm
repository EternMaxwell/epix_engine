export module epix.sfml.render;

import epix.core;

namespace sfml::render {
/** @brief Plugin that registers SFML-specific render target (surface)
 * creation for the render pipeline. */
export struct SFMLRenderPlugin {
    void build(core::App& app);
};
}  // namespace sfml::render

namespace sfml {
export using render::SFMLRenderPlugin;
}
