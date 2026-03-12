export module epix.window;

export import :events;
export import :structs;
export import :system;

export namespace window {
/** @brief Determines when the application should exit based on window
 * state. */
enum class ExitCondition {
    /** @brief Exit when all windows are closed. */
    OnAllClosed,
    /** @brief Exit when the primary window is closed. */
    OnPrimaryClosed,
    /** @brief Never auto-exit due to window closure. */
    None,
};
/** @brief Plugin that creates the primary window and registers window
 * lifecycle systems.
 *
 * Set `primary_window` to std::nullopt to skip creating a default window.
 */
struct WindowPlugin {
    /** @brief Configuration for the primary window. Nullopt skips
     * creation. */
    std::optional<Window> primary_window = Window{};
    /** @brief When the application should automatically exit. */
    ExitCondition exit_condition = ExitCondition::OnPrimaryClosed;
    /** @brief Whether to despawn window entities on close request. */
    bool close_when_requested = true;
    void build(core::App& app);
    void finish(core::App& app);
};

/** @brief Debug system that logs all window events to the logger. */
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