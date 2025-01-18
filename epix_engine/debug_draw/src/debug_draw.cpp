#include "epix/render/debug.h"

using namespace epix::render;

EPIX_API void debug::vulkan::DebugRenderPlugin::build(App& app) {
    if (app.get_plugin<render_vk::RenderVKPlugin>()) {
        app.add_system(Startup, systems::create_line_drawer);
        app.add_system(Startup, systems::create_point_drawer);
        app.add_system(Startup, systems::create_triangle_drawer);
        app.add_system(Exit, systems::destroy_line_drawer);
        app.add_system(Exit, systems::destroy_point_drawer);
        app.add_system(Exit, systems::destroy_triangle_drawer);
    } else if (app.get_plugin<render::vulkan2::VulkanPlugin>()) {
        app.add_system(
               PreStartup, render::debug::vulkan2::systems::create_pipelines
        )
            .after(render::vulkan2::systems::create_context);
        app.add_system(
            Extraction, render::debug::vulkan2::systems::extract_pipelines
        );
        app.add_system(
               PostExit, render::debug::vulkan2::systems::destroy_pipelines
        )
            .before(render::vulkan2::systems::destroy_context);
    }
}