#include "epix/imgui.h"

using namespace epix::prelude;
using namespace epix::imgui;

EPIX_API void ImGuiPluginVK::build(App& app) {
    app.add_system(PreStartup, systems::insert_imgui_ctx);
    app.add_system(Startup, systems::init_imgui.worker("single"));
    app.add_system(Extraction, systems::extract_imgui_ctx);
    app.add_system(
        PreRender, systems::begin_imgui.after(
                       epix::render::vulkan2::systems::get_next_image
                   )
    );
    app.add_system(
        PostRender,
        systems::end_imgui.before(epix::render::vulkan2::systems::present_frame)
    );
    app.add_system(Exit, systems::deinit_imgui.worker("single"));
}
