export module epix.glfw.render;

import epix.core;

namespace glfw::render {
export struct GlfwRenderPlugin {
    void build(core::App& app);
};
}  // namespace glfw::render

namespace glfw {
export using render::GlfwRenderPlugin;
}