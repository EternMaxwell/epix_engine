#include "epix/window.hpp"
#include "epix/window/system.hpp"

using namespace epix::window;

void WindowPlugin::build(epix::App& app) {
    using namespace events;
    app.add_events<WindowResized>()
        .add_events<WindowMoved>()
        .add_events<WindowCreated>()
        .add_events<WindowClosed>()
        .add_events<WindowCloseRequested>()
        .add_events<WindowDestroyed>()
        .add_events<CursorMoved>()
        .add_events<CursorEntered>()
        .add_events<ReceivedCharacter>()
        .add_events<WindowFocused>()
        .add_events<FileDrop>();

    if (primary_window) {
        auto window = app.world_mut().spawn(primary_window.value(), PrimaryWindow{});
    }

    if (exit_condition == ExitCondition::OnAllClosed) {
        app.add_systems(PostUpdate, into(exit_on_all_closed).set_name("exit on all closed"));
    } else if (exit_condition == ExitCondition::OnPrimaryClosed) {
        app.add_systems(PostUpdate, into(exit_on_primary_closed).set_name("exit on primary closed"));
    }

    if (close_when_requested) {
        app.add_systems(Update, into(close_requested).set_name("close requested windows"));
    }
}