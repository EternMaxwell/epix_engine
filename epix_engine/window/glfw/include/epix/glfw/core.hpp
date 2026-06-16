#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <GLFW/glfw3.h>

#include <epix/assets.hpp>
#include <epix/core.hpp>
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

namespace epix::glfw {
struct Resized {
    int width;
    int height;
};
struct KeyInput {
    int key;
    int scancode;
    int action;
    int mods;
};
struct CursorPos {
    double x;
    double y;
};
struct CursorEnter {
    bool entered;
};
struct MouseButton {
    int button;
    int action;
    int mods;
};
struct Scroll {
    double xoffset;
    double yoffset;
};
/** @brief Event carrying file paths dropped onto a GLFW window. */
EPIX_EXPORT struct PathDrop {
    /** @brief File system paths of the dropped files. */
    std::vector<std::string> paths;
};
/** @brief Event carrying a Unicode character received by a GLFW window. */
EPIX_EXPORT struct ReceivedCharacter {
    /** @brief The Unicode code point of the received character. */
    char32_t character;
};
/**
 * @brief User data for the glfw window.
 *
 * This will include cached events for later update the event queue in app.
 */
struct UserData {
    epix::core::ConQueue<Resized> resized;
    epix::core::ConQueue<KeyInput> key_input;
    epix::core::ConQueue<CursorPos> cursor_pos;
    epix::core::ConQueue<CursorEnter> cursor_enter;
    epix::core::ConQueue<MouseButton> mouse_button;
    epix::core::ConQueue<Scroll> scroll;
    epix::core::ConQueue<PathDrop> drops;
    epix::core::ConQueue<ReceivedCharacter> received_character;
    epix::core::ConQueue<bool> focused;
    epix::core::ConQueue<std::pair<int, int>> moved;
};

int map_key_to_glfw(input::KeyCode key);
int map_mouse_button_to_glfw(input::MouseButton button);
input::KeyCode map_glfw_key_to_input(int key);
input::MouseButton map_glfw_mouse_button_to_input(int button);
/** @brief Resource mapping entity IDs to their native GLFW window
 * pointers. */
EPIX_EXPORT struct GLFWwindows : public std::unordered_map<epix::core::Entity, GLFWwindow*> {};

/** @brief Event requesting the clipboard text to be set. */
EPIX_EXPORT struct SetClipboardString {
    /** @brief The text to write to the clipboard. */
    std::string text;
};
/** @brief Resource providing read access to the system clipboard. */
EPIX_EXPORT struct Clipboard {
   private:
    std::string text;

   public:
    /** @brief Get the current clipboard text. */
    const std::string& get_text() const noexcept;
    /** @brief System that reads clipboard text from the OS. */
    static void update(epix::core::ResMut<Clipboard> clipboard);
    /** @brief System that writes pending clipboard text to the OS. */
    static void set_text(epix::core::EventReader<SetClipboardString> events);
};
struct CachedWindowPosSize {
    int pos_x  = 0;
    int pos_y  = 0;
    int width  = 0;
    int height = 0;
};
/** @brief Application runner that drives the GLFW event loop.
 *
 * Manages GLFW window creation, event polling, and per-frame stepping.
 * Supports delegating rendering to a sub-app on a separate thread via
 * `set_render_app()`.
 */
EPIX_EXPORT struct GLFWRunner : public epix::core::AppRunner {
   public:
    GLFWRunner(epix::core::App& app);
    bool step(epix::core::App& app) override;
    void exit(epix::core::App& app) override;

    /** @brief Set the sub-app label used for rendering on a separate thread. */
    void set_render_app(const epix::core::AppLabel& label) noexcept { render_app_label = label; }
    /** @brief Clear the render sub-app, running everything on the main thread. */
    void reset_render_app() noexcept { render_app_label = std::nullopt; }

    /** @brief Append an extra system to run each frame. */
    void append_system(std::unique_ptr<epix::core::System<std::tuple<>, void>> system) {
        extra_systems.push_back(std::move(system));
    }

   private:
    std::unique_ptr<epix::core::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<epix::core::System<std::tuple<>, void>> remove_window;
    epix::core::FilteredAccessSet exit_access;
    epix::core::FilteredAccessSet remove_access;
    std::unique_ptr<epix::core::System<std::tuple<>, void>> create_windows_system, update_size_system,
        update_pos_system, toggle_window_mode_system, update_window_states_system, destroy_windows_system,
        send_cached_events_system, clipboard_set_text_system, clipboard_update_system;
    std::vector<std::unique_ptr<epix::core::System<std::tuple<>, void>>> extra_systems;
    std::optional<std::future<std::unique_ptr<epix::core::App>>> render_app_future;
    std::optional<epix::core::AppLabel> render_app_label;
};
/** @brief Plugin that registers the GLFW windowing backend, including
 * window creation, event dispatch, and lifecycle systems. */
EPIX_EXPORT struct GLFWPlugin {
    void attach(epix::core::App& app);
    void detach(epix::core::App& app);

    /** @brief System that syncs window size from GLFW to the Window component. */
    static void update_size(
        epix::core::Query<
            epix::core::Item<epix::core::Entity, epix::core::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::core::ResMut<GLFWwindows> glfw_windows);
    /** @brief System that syncs window position from GLFW. */
    static void update_pos(
        epix::core::Commands commands,
        epix::core::Query<epix::core::Item<epix::core::Entity,
                                           epix::core::Mut<window::Window>,
                                           epix::core::Opt<const window::CachedWindow&>,
                                           epix::core::Opt<const epix::core::Parent&>>> windows,
        epix::core::ResMut<GLFWwindows> glfw_windows,
        epix::core::Local<std::unordered_map<epix::core::Entity, std::pair<int, int>>> pending_window_positions);
    /** @brief System that creates native GLFW windows for new Window entities. */
    static void create_windows(
        epix::core::Commands cmd,
        epix::core::Query<epix::core::Item<epix::core::Entity,
                                           epix::core::Mut<window::Window>,
                                           epix::core::Opt<epix::core::Ref<epix::core::Parent>>,
                                           epix::core::Opt<epix::core::Ref<epix::core::Children>>>> windows,
        epix::core::ResMut<GLFWwindows> glfw_windows,
        epix::core::EventWriter<window::WindowCreated> window_created);
    /** @brief System that applies window state changes (title, cursor, icon, etc.). */
    static void update_window_states(
        epix::core::Query<
            epix::core::Item<epix::core::Entity, epix::core::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::core::Res<assets::Assets<image::Image>> images,
        epix::core::ResMut<GLFWwindows> glfw_windows);
    /** @brief System that toggles between windowed and fullscreen modes. */
    static void toggle_window_mode(
        epix::core::Query<
            epix::core::Item<epix::core::Entity, epix::core::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::core::ResMut<GLFWwindows> glfw_windows,
        epix::core::Local<std::unordered_map<epix::core::Entity, CachedWindowPosSize>> cached_window_sizes);
    /** @brief Poll all pending GLFW events. */
    static void poll_events();
    /** @brief System that dispatches cached GLFW events to the ECS event system. */
    static void send_cached_events(epix::core::Query<epix::core::Item<const window::CachedWindow&>> cached_windows,
                                   epix::core::ResMut<GLFWwindows> glfw_windows,
                                   epix::core::EventWriter<window::WindowResized> window_resized,
                                   epix::core::EventWriter<window::WindowCloseRequested> window_close_requested,
                                   epix::core::EventWriter<window::CursorMoved> cursor_moved,
                                   epix::core::EventWriter<window::CursorEntered> cursor_entered,
                                   epix::core::EventWriter<window::FileDrop> file_drop,
                                   epix::core::EventWriter<window::ReceivedCharacter> received_character,
                                   epix::core::EventWriter<window::WindowFocused> window_focused,
                                   epix::core::EventWriter<window::WindowMoved> window_moved,
                                   std::optional<epix::core::EventWriter<input::KeyInput>> key_input,
                                   std::optional<epix::core::EventWriter<input::MouseButtonInput>> mouse_button_input,
                                   std::optional<epix::core::EventWriter<input::MouseMove>> mouse_move_input,
                                   std::optional<epix::core::EventWriter<input::MouseScroll>> scroll_input);
    /** @brief System that destroys GLFW windows for removed Window entities. */
    static void destroy_windows(epix::core::Query<epix::core::Item<epix::core::Entity, const window::Window&>> windows,
                                epix::core::ResMut<GLFWwindows> glfw_windows,
                                epix::core::EventWriter<window::WindowClosed> window_closed,
                                epix::core::EventWriter<window::WindowDestroyed> window_destroyed);

   private:
    static GLFWwindow* create_window(epix::core::Entity id, window::Window& window);
};
}  // namespace epix::glfw