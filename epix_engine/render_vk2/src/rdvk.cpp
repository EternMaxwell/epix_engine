#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
EPIX_API RenderVKPlugin& RenderVKPlugin::set_debug_callback(bool debug) {
    debug_callback = debug;
    return *this;
}
EPIX_API void RenderVKPlugin::build(epix::App& app) {
    auto window_plugin = app.get_plugin<window::WindowPlugin>();
    window_plugin->primary_desc().set_hints(
        {{GLFW_RESIZABLE, GLFW_TRUE}, {GLFW_CLIENT_API, GLFW_NO_API}}
    );
    app.add_system(PreStartup, systems::create_context)
        .in_set(window::WindowStartUpSets::after_window_creation);
    app.add_system(Extraction, systems::extract_context);
    app.add_system(
           Prepare, systems::recreate_swap_chain, systems::get_next_image
    )
        .chain();
    app.add_system(
           PostRender, systems::present_frame, systems::clear_extracted_context
    )
        .chain();
    app.add_system(PostExit, systems::destroy_context);
}
}  // namespace epix::render::vulkan2