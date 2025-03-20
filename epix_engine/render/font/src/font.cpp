#include "epix/font.h"

using namespace epix;
using namespace epix::font;

EPIX_API void FontPlugin::build(App& app) {
    if (app.get_plugin<epix::render::vulkan2::VulkanPlugin>()) {
        app.add_system(
            PreStartup,
            epix::font::vulkan2::systems::insert_font_atlas
                .after(epix::render::vulkan2::systems::create_context)
                .after(epix::render::vulkan2::systems::create_res_manager)
        );
        app.add_system(
            PreExtract, epix::font::vulkan2::systems::extract_font_atlas
        );
        app.add_system(
            PostExit, epix::font::vulkan2::systems::destroy_font_atlas.before(
                          epix::render::vulkan2::systems::destroy_context
                      )
        );
    }
}