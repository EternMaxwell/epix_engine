#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
EPIX_API VulkanPlugin& VulkanPlugin::set_debug_callback(bool debug) {
    debug_callback = debug;
    return *this;
}
EPIX_API void VulkanPlugin::build(epix::App& app) {
    auto window_plugin = app.get_plugin<window::WindowPlugin>();
    window_plugin->primary_desc().set_hints(
        {{GLFW_RESIZABLE, GLFW_TRUE}, {GLFW_CLIENT_API, GLFW_NO_API}}
    );
    app.add_system(
        PreStartup,
        bundle(systems::create_context, systems::create_res_manager)
            .in_set(window::WindowStartUpSets::after_window_creation)
            .chain()
    );
    app.add_system(
        PreExtract,
        bundle(systems::extract_context, systems::extract_res_manager)
    );
    app.add_system(
        Prepare, chain(systems::recreate_swap_chain, systems::get_next_image)
    );
    app.add_system(
        PostRender,
        chain(systems::present_frame, systems::clear_extracted_context)
    );
    app.add_system(
        PostExit, chain(systems::destroy_res_manager, systems::destroy_context)
    );
}
}  // namespace epix::render::vulkan2