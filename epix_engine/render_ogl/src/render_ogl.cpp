#include "epix/render_ogl.h"

namespace epix::render::ogl {
EPIX_API void RenderGlPlugin::build(App& app) {
    app.add_system(
        PreStartup, into(systems::context_creation)
                        .worker("single")
                        .after(window::systems::init_glfw)
    );
    app.add_system(Prepare, into(systems::context_creation).worker("single"));
    app.add_system(
        Prepare, into(systems::clear_color)
                     .worker("single")
                     .after(systems::context_creation)
    );
    app.add_system(
        Prepare, into(systems::update_viewport)
                     .before(systems::clear_color)
                     .worker("single")
                     .after(systems::context_creation)
    );
    app.add_system(PostRender, into(systems::swap_buffers).worker("single"));
}
}  // namespace epix::render::ogl