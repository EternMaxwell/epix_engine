#include "epix/window.h"

using namespace epix::window;

EPIX_API std::optional<epix::Entity>& Focus::operator*() { return focus; }
EPIX_API const std::optional<epix::Entity>& Focus::operator*() const {
    return focus;
}

EPIX_API void WindowPlugin::build(epix::App& app) {
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

    app.insert_resource<Focus>(Focus{std::nullopt});

    if (primary_window) {
        auto window = app.world(epix::app::MainWorld)
                          .spawn(primary_window.value(), PrimaryWindow{});
        if (auto focus =
                app.world(epix::app::MainWorld).get_resource<Focus>()) {
            ***focus = window;
        }
    }

    if (exit_condition == ExitCondition::OnAllClosed) {
        app.add_systems(PostUpdate, into(exit_on_all_closed));
    } else if (exit_condition == ExitCondition::OnPrimaryClosed) {
        app.add_systems(PostUpdate, into(exit_on_primary_closed));
    }

    if (close_when_requested) {
        app.add_systems(Update, into(close_requested));
    }
}