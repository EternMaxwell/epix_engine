export module epix.window;

export import :events;
export import :structs;
export import :system;

export namespace window {
enum class ExitCondition {
    OnAllClosed,
    OnPrimaryClosed,
    None,
};
struct WindowPlugin {
    std::optional<Window> primary_window = Window{};
    ExitCondition exit_condition         = ExitCondition::OnPrimaryClosed;
    bool close_when_requested            = true;
    void build(core::App& app);
    void finish(core::App& app);
};

void log_events(core::EventReader<WindowResized> resized,
                core::EventReader<WindowMoved> moved,
                core::EventReader<WindowCreated> created,
                core::EventReader<WindowClosed> closed,
                core::EventReader<WindowCloseRequested> close_requested,
                core::EventReader<WindowDestroyed> destroyed,
                core::EventReader<CursorMoved> cursor_moved,
                core::EventReader<CursorEntered> cursor_entered,
                core::EventReader<FileDrop> file_drop,
                core::EventReader<ReceivedCharacter> received_character,
                core::EventReader<WindowFocused> window_focused,
                core::Query<core::Item<const Window&>> windows);
}  // namespace window