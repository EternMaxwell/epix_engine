export module epix.sfml.render;

import epix.core;

namespace sfml::render {
export struct SFMLRenderPlugin {
    void build(core::App& app);
};
}  // namespace sfml::render

namespace sfml {
export using render::SFMLRenderPlugin;
}
