#include "epix/render_vk.h"

using namespace epix::prelude;
using namespace epix::render_vk;

EPIX_API void RenderVKPlugin::build(App& app) {
    auto window_plugin = app.get_plugin<window::WindowPlugin>();
    window_plugin->primary_desc().set_vsync(vsync).set_hints(
        {{GLFW_RESIZABLE, GLFW_TRUE}, {GLFW_CLIENT_API, GLFW_NO_API}}
    );

    app.add_system(PreStartup, systems::create_context)
        .in_set(window::WindowStartUpSets::after_window_creation);
    app.add_system(Prepare, systems::recreate_swap_chain);
    app.add_system(Prepare, systems::get_next_image)
        .after(systems::recreate_swap_chain);
    app.add_system(PostRender, systems::present_frame);
    app.add_system(PostExit, systems::destroy_context);
}