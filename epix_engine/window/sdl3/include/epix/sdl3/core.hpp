#pragma once

// The SDL3 backend owns the main loop through SDL's application callbacks, and
// it must not let SDL rename the host program's entry point.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/window.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

/** @brief Native SDL window handle.
 *
 * Only forward declared: SDL is a private implementation detail of the engine,
 * so no public header pulls it in and a consumer cannot end up with a second,
 * independent copy of SDL inside one process. */
struct SDL_Window;

namespace epix::sdl3 {

/** @brief SDL events staged for the frame that is currently running.
 *
 * SDL delivers events outside the ECS, so they are collected here as raw
 * `SDL_Event` bytes and translated into ECS events by
 * `SDL3Plugin::send_cached_events` during the frame. The representation stays
 * opaque so the SDL headers remain private to the backend.
 */
struct SDL3Events {
    /** @brief Consecutive, byte-for-byte copies of `SDL_Event`. */
    std::vector<std::byte> events;
};

/** @brief Event carrying file paths dropped onto an SDL window. */
EPIX_EXPORT struct PathDrop {
    /** @brief File system paths of the dropped files. */
    std::vector<std::string> paths;
};
/** @brief Event carrying a Unicode character received by an SDL window. */
EPIX_EXPORT struct ReceivedCharacter {
    /** @brief The Unicode code point of the received character. */
    char32_t character;
};

/** @brief Map an engine key code to the SDL key code for the same physical key.
 * SDL key codes are `Uint32`. */
EPIX_API std::uint32_t map_key_to_sdl3(input::KeyCode key);
/** @brief Map an SDL key code to the engine key code for the same physical
 * key. */
EPIX_API input::KeyCode map_sdl3_key_to_input(std::uint32_t key);
/** @brief Map an engine mouse button to the SDL mouse button index. */
EPIX_API std::uint8_t map_mouse_button_to_sdl3(input::MouseButton button);
/** @brief Map an SDL mouse button index to the engine mouse button. */
EPIX_API input::MouseButton map_sdl3_mouse_button_to_input(std::uint8_t button);

/** @brief Resource mapping entity IDs to their native SDL window pointers. */
EPIX_EXPORT struct SDL3windows : public std::unordered_map<epix::ecs::Entity, SDL_Window*> {};

/** @brief Event requesting the clipboard text to be set. */
EPIX_EXPORT struct SetClipboardString {
    /** @brief The text to write to the clipboard. */
    std::string text;
};
/** @brief Resource providing read access to the system clipboard. */
EPIX_EXPORT struct EPIX_API Clipboard {
   private:
    std::string text;

   public:
    /** @brief Get the current clipboard text. */
    const std::string& get_text() const noexcept;
    /** @brief System that reads clipboard text from the OS. */
    static void update(epix::ecs::ResMut<Clipboard> clipboard);
    /** @brief System that writes pending clipboard text to the OS. */
    static void set_text(epix::ecs::EventReader<SetClipboardString> events);
};

/** @brief Snapshot of a window's position and size, used to restore it when a
 * window leaves fullscreen. */
struct CachedWindowPosSize {
    int pos_x  = 0;
    int pos_y  = 0;
    int width  = 0;
    int height = 0;
};

/** @brief Application runner that drives the app from SDL's application
 * callbacks.
 *
 * The engine runs inside `SDL_EnterAppMainCallbacks` rather than in a loop of
 * its own. SDL then keeps driving it while a Windows window is being dragged or
 * resized: the modal move/resize loop blocks the application's own message
 * pump, and SDL answers that with a timer that re-enters the application
 * callbacks, so frames keep being produced instead of the window freezing.
 *
 * Rendering can still be delegated to a sub-app on a separate thread via
 * `set_render_app()`.
 */
EPIX_EXPORT struct EPIX_API SDL3Runner : public epix::app::AppRunner {
   public:
    SDL3Runner(epix::app::App& app);
    bool step(epix::app::App& app) override;
    void exit(epix::app::App& app) override;

    /** @brief Set the sub-app label used for rendering on a separate thread. */
    void set_render_app(const epix::app::AppLabel& label) noexcept { render_app_label = label; }
    /** @brief Clear the render sub-app, running everything on the main thread. */
    void reset_render_app() noexcept { render_app_label = std::nullopt; }

    /** @brief Append an extra system to run each frame. */
    void append_system(std::unique_ptr<epix::ecs::System<std::tuple<>, void>> system) {
        extra_systems.push_back(std::move(system));
    }

    /** @brief Advance the engine by one frame. SDL calls this from its
     * iteration callback, including from inside a modal move/resize loop.
     * @return True to keep running, false when the app has asked to exit. */
    bool iterate();
    /** @brief Stage one SDL event for this frame's systems.
     * @param sdl_event An `SDL_Event`, passed as an opaque pointer so this
     * header needs no SDL types. */
    void stage_event(const void* sdl_event);
    /** @brief SDL is about to shut itself down and destroy every window. */
    void on_sdl_quit();

   private:
    void shutdown_render_app(epix::app::App& app);
    void run_frame_systems(epix::ecs::World& world);

    std::unique_ptr<epix::ecs::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<epix::ecs::System<std::tuple<>, void>> remove_window;
    epix::ecs::FilteredAccessSet exit_access;
    epix::ecs::FilteredAccessSet remove_access;
    std::unique_ptr<epix::ecs::System<std::tuple<>, void>> create_windows_system, update_size_system, update_pos_system,
        toggle_window_mode_system, update_window_states_system, destroy_windows_system, destroy_all_windows_system,
        send_cached_events_system, clipboard_set_text_system, clipboard_update_system;
    std::vector<std::unique_ptr<epix::ecs::System<std::tuple<>, void>>> extra_systems;
    std::optional<std::future<std::unique_ptr<epix::app::App>>> render_app_future;
    std::optional<epix::app::AppLabel> render_app_label;

    epix::app::App* app         = nullptr;
    bool main_loop_finished     = false;
};

/** @brief Plugin that registers the SDL3 windowing backend, including window
 * creation, event dispatch, and lifecycle systems. */
EPIX_EXPORT struct EPIX_API SDL3Plugin {
    void attach(epix::app::App& app);
    void detach(epix::app::App& app);

    /** @brief System that syncs window size from SDL to the Window component. */
    static void update_size(
        epix::ecs::Query<
            epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::ecs::ResMut<SDL3windows> sdl_windows);
    /** @brief System that syncs window position from SDL. */
    static void update_pos(
        epix::ecs::Commands commands,
        epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                         epix::ecs::Mut<window::Window>,
                                         epix::ecs::Opt<const window::CachedWindow&>,
                                         epix::ecs::Opt<const epix::ecs::Parent&>>> windows,
        epix::ecs::ResMut<SDL3windows> sdl_windows,
        epix::ecs::Local<std::unordered_map<epix::ecs::Entity, std::pair<int, int>>> pending_window_positions);
    /** @brief System that creates native SDL windows for new Window entities. */
    static void create_windows(
        epix::ecs::Commands cmd,
        epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                         epix::ecs::Mut<window::Window>,
                                         epix::ecs::Opt<epix::ecs::Ref<epix::ecs::Parent>>,
                                         epix::ecs::Opt<epix::ecs::Ref<epix::ecs::Children>>>> windows,
        epix::ecs::ResMut<SDL3windows> sdl_windows,
        epix::ecs::EventWriter<window::WindowCreated> window_created);
    /** @brief System that applies window state changes (title, cursor, icon,
     * etc.). */
    static void update_window_states(
        epix::ecs::Query<
            epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::ecs::Res<assets::Assets<image::Image>> images,
        epix::ecs::ResMut<SDL3windows> sdl_windows);
    /** @brief System that toggles between windowed and fullscreen modes. */
    static void toggle_window_mode(
        epix::ecs::Query<
            epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::ecs::ResMut<SDL3windows> sdl_windows,
        epix::ecs::Local<std::unordered_map<epix::ecs::Entity, CachedWindowPosSize>> cached_window_sizes);
    /** @brief System that dispatches staged SDL events to the ECS event
     * system. */
    static void send_cached_events(epix::ecs::Res<SDL3Events> events,
                                   epix::ecs::ResMut<SDL3windows> sdl_windows,
                                   epix::ecs::EventWriter<window::WindowResized> window_resized,
                                   epix::ecs::EventWriter<window::WindowScaleFactorChanged> window_scale_factor_changed,
                                   epix::ecs::EventWriter<window::WindowCloseRequested> window_close_requested,
                                   epix::ecs::EventWriter<window::CursorMoved> cursor_moved,
                                   epix::ecs::EventWriter<window::CursorEntered> cursor_entered,
                                   epix::ecs::EventWriter<window::FileDrop> file_drop,
                                   epix::ecs::EventWriter<window::ReceivedCharacter> received_character,
                                   epix::ecs::EventWriter<window::WindowFocused> window_focused,
                                   epix::ecs::EventWriter<window::WindowMoved> window_moved,
                                   std::optional<epix::ecs::EventWriter<input::KeyInput>> key_input,
                                   std::optional<epix::ecs::EventWriter<input::MouseButtonInput>> mouse_button_input,
                                   std::optional<epix::ecs::EventWriter<input::MouseMove>> mouse_move_input,
                                   std::optional<epix::ecs::EventWriter<input::MouseScroll>> scroll_input);
    /** @brief System that destroys SDL windows for removed Window entities. */
    static void destroy_windows(epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const window::Window&>> windows,
                                epix::ecs::ResMut<SDL3windows> sdl_windows,
                                epix::ecs::EventWriter<window::WindowClosed> window_closed,
                                epix::ecs::EventWriter<window::WindowDestroyed> window_destroyed);
    /** @brief Destroy every native window SDL still tracks and report them as
     * closed. */
    static void destroy_all_windows(epix::ecs::ResMut<SDL3windows> sdl_windows,
                                    epix::ecs::EventWriter<window::WindowClosed> window_closed,
                                    epix::ecs::EventWriter<window::WindowDestroyed> window_destroyed);

   private:
    static SDL_Window* create_window(epix::ecs::Entity id, window::Window& window);
};
}  // namespace epix::sdl3
