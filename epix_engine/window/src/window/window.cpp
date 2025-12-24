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
}
void WindowPlugin::finish(epix::App& app) {
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

void epix::window::log_events(epix::EventReader<events::WindowResized> resized,
                              epix::EventReader<events::WindowMoved> moved,
                              epix::EventReader<events::WindowCreated> created,
                              epix::EventReader<events::WindowClosed> closed,
                              epix::EventReader<events::WindowCloseRequested> close_requested,
                              epix::EventReader<events::WindowDestroyed> destroyed,
                              epix::EventReader<events::CursorMoved> cursor_moved,
                              epix::EventReader<events::CursorEntered> cursor_entered,
                              epix::EventReader<events::FileDrop> file_drop,
                              epix::EventReader<events::ReceivedCharacter> received_character,
                              epix::EventReader<events::WindowFocused> window_focused,
                              epix::Query<epix::Item<const window::Window&>> windows) {
    for (auto&& [id, width, height] : resized.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} resized to {}x{}", window.title, width, height);
        } else {
            spdlog::info("Window {} resized to {}x{}", id.index, width, height);
        }
    }
    for (auto&& [id, move] : moved.read()) {
        auto&& window_t = windows.get(id);
        auto&& [x, y]   = move;
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} moved to {}x{}", window.title, x, y);
        } else {
            spdlog::info("Window {} moved to {}x{}", id.index, x, y);
        }
    }
    for (auto&& [id] : created.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} created", window.title);
        } else {
            spdlog::info("Window {} created", id.index);
        }
    }
    for (auto&& [id] : closed.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} closed", window.title);
        } else {
            spdlog::info("Window {} closed", id.index);
        }
    }
    for (auto&& [id] : close_requested.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} close requested", window.title);
        } else {
            spdlog::info("Window {} close requested", id.index);
        }
    }
    for (auto&& [id] : destroyed.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} destroyed", window.title);
        } else {
            spdlog::info("Window {} destroyed", id.index);
        }
    }
    for (auto&& [id, pos, delta] : cursor_moved.read()) {
        auto&& window_t = windows.get(id);
        auto&& [x, y]   = pos;
        auto&& [dx, dy] = delta;
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} cursor moved to {}x{} delta {}x{}", window.title, x, y, dx, dy);
        } else {
            spdlog::info("Window {} cursor moved to {}x{} delta {}x{}", id.index, x, y, dx, dy);
        }
    }
    for (auto&& [id, enter] : cursor_entered.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} cursor entered {}", window.title, enter);
        } else {
            spdlog::info("Window {} cursor entered {}", id.index, enter);
        }
    }
    for (auto&& [id, paths] : file_drop.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} file drop {}", window.title, std::views::all(paths));
        } else {
            spdlog::info("Window {} file drop {}", id.index, std::views::all(paths));
        }
    }
    for (auto&& [id, character] : received_character.read()) {
        auto&& window_t = windows.get(id);
        uint32_t code   = character;
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} received character {}", window.title, code);
        } else {
            spdlog::info("Window {} received character {}", id.index, code);
        }
    }
    for (auto&& [id, focused] : window_focused.read()) {
        auto&& window_t = windows.get(id);
        if (window_t) {
            auto&& [window] = *window_t;
            spdlog::info("Window {} focused {}", window.title, focused);
        } else {
            spdlog::info("Window {} focused {}", id.index, focused);
        }
    }
}