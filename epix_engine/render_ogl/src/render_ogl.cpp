#include "epix/render_ogl.h"

namespace epix::render::ogl {
EPIX_API void RenderGlPlugin::build(App& app) {
    app.add_system(
        PreStartup, systems::context_creation.worker("single").after(
                        window::systems::init_glfw
                    )
    );
    app.add_system(Prepare, systems::context_creation.worker("single"));
    app.add_system(
        Prepare,
        systems::clear_color.worker("single").after(systems::context_creation)
    );
    app.add_system(
        Prepare, systems::update_viewport.before(systems::clear_color)
                     .worker("single")
                     .after(systems::context_creation)
    );
    app.add_system(PostRender, systems::swap_buffers.worker("single"));
}
}  // namespace epix::render::ogl