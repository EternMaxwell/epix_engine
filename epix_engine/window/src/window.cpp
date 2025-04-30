#include "epix/window.h"
#include "epix/window/systems.h"

using namespace epix::window::systems;
using namespace epix::window::events;

using namespace epix::window;
using namespace epix;

EPIX_API WindowDescription& WindowPlugin::primary_desc() {
    return primary_window_description;
}

EPIX_API void epix::window::WindowPlugin::build(App& app) {
    app.add_plugin(LoopPlugin{});
    app.add_events<events::AnyWindowClose>();
    app.add_events<events::NoWindowExists>();
    app.add_events<events::PrimaryWindow>();
    app.add_events<events::MouseScroll>();
    app.add_events<events::CursorMove>();
    app.configure_sets(sets(
                           WindowStartUpSets::glfw_initialization,
                           WindowStartUpSets::window_creation,
                           WindowStartUpSets::after_window_creation
                       )
                           .chain())
        .configure_sets(sets(
                            WindowPreRenderSets::before_create,
                            WindowPreRenderSets::window_creation,
                            WindowPreRenderSets::after_create
        )
                            .chain())
        .add_systems(
            epix::PreStartup,
            into(init_glfw)
                .in_set(WindowStartUpSets::glfw_initialization)
                .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(
            epix::PreStartup,
            into(create_window_thread_pool)
                .in_set(WindowStartUpSets::glfw_initialization)
                .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(
            epix::PreStartup,
            into(insert_primary_window).before(systems::create_window)
        )
        .add_systems(
            epix::PreStartup, into(systems::create_window)
                                  .in_set(WindowStartUpSets::window_creation)
                                  .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(
            epix::First, into(systems::create_window)
                             .before(poll_events)
                             .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(
            epix::First,
            into(poll_events).set_executor(ExecutorType::SingleThread)
        )
        .add_systems(epix::First, into(scroll_events).after(poll_events))
        .add_systems(
            epix::First, into(update_window_state)
                             .after(poll_events)
                             .before(close_window)
                             .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(epix::First, into(close_window))
        .add_systems(
            epix::Last, into(primary_window_close)
                            .before(no_window_exists)
                            .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(
            epix::Last, into(window_close)
                            .before(no_window_exists)
                            .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(
            epix::Last, into(no_window_exists)
                            .before(exit_on_no_window)
                            .set_executor(ExecutorType::SingleThread)
        )
        .add_systems(epix::Last, into(exit_on_no_window))
        .add_systems(
            epix::PreExit,
            into(systems::close_window).set_executor(ExecutorType::SingleThread)
        );
}
