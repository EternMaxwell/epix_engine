export module epix.render;

import epix.core;
import webgpu;
import std;

export import :schedule;
export import :extract;
export import :assets;
export import :graph;

namespace render {
/**
 * @brief Resource for anonymous surface that is used for requesting adapter/device.
 * Since webgpu requires a surface to request an adapter, we provide this resource to let window implementations to
 * give a functor that creates a surface from the instance. It is recommanded that the functor will destruct the
 * temporary window after we are done with requesting the adapter/device and releasing this resource.
 */
export struct AnonymousSurface {
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
};
export struct RenderPlugin {
    int validation = 0;
    /**
     * @brief Set the validation level for the render plugin.
     * 0 - No validation
     * 1 - Nvrhi validation
     * 2 - Vulkan validation layers
     * @param level the validation level to set
     */
    RenderPlugin& set_validation(int level = 0);
    void build(core::App&);
    void finalize(core::App&);
};
void render_system(core::World& world);
}  // namespace render