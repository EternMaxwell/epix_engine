#include "epix/window.h"
#include "epix/window/systems.h"


using namespace epix::window::systems;
using namespace epix::window::events;

using namespace epix::window;
using namespace epix::prelude;

EPIX_API WindowDescription& WindowPlugin::primary_desc() {
    return primary_window_description;
}

EPIX_API void epix::window::WindowPlugin::build(App& app) {
    app.enable_loop();
    app.add_event<events::AnyWindowClose>();
    app.add_event<events::NoWindowExists>();
    app.add_event<events::PrimaryWindow>();
    app.add_event<events::MouseScroll>();
    app.add_event<events::CursorMove>();
    app->configure_sets(
           WindowStartUpSets::glfw_initialization,
           WindowStartUpSets::window_creation,
           WindowStartUpSets::after_window_creation
    )
        ->configure_sets(
            WindowPreRenderSets::before_create,
            WindowPreRenderSets::window_creation,
            WindowPreRenderSets::after_create
        )
        ->add_system(
            epix::PreStartup,
            into(init_glfw)
                .in_set(WindowStartUpSets::glfw_initialization)
                .worker("single")
        )
        ->add_system(
            epix::PreStartup,
            into(create_window_thread_pool)
                .in_set(WindowStartUpSets::glfw_initialization)
                .worker("single")
        )
        ->add_system(
            epix::PreStartup,
            into(insert_primary_window).before(systems::create_window)
        )
        ->add_system(
            epix::PreStartup, into(systems::create_window)
                                  .in_set(WindowStartUpSets::window_creation)
                                  .worker("single")
        )
        ->add_system(
            epix::First,
            into(systems::create_window).before(poll_events).worker("single")
        )
        ->add_system(epix::First, into(poll_events).worker("single"))
        ->add_system(epix::First, into(scroll_events).after(poll_events))
        ->add_system(
            epix::First, into(update_window_state)
                             .after(poll_events)
                             .before(close_window)
                             .worker("single")
        )
        ->add_system(epix::First, close_window)
        ->add_system(
            epix::Last,
            into(primary_window_close).before(no_window_exists).worker("single")
        )
        ->add_system(
            epix::Last,
            into(window_close).before(no_window_exists).worker("single")
        )
        ->add_system(
            epix::Last,
            into(no_window_exists).before(exit_on_no_window).worker("single")
        )
        ->add_system(epix::Last, exit_on_no_window)
        ->add_system(
            epix::PreExit, into(systems::close_window).worker("single")
        );
}
