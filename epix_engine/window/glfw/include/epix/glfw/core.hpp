#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <GLFW/glfw3.h>

#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/window.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace epix::glfw {

/** @brief Thread-safe concurrent queue. Used to pass events from GLFW
 *  callbacks to the main thread via UserData. */
template <typename T>
struct ConQueue {
    template <typename... Args>
    void emplace(Args&&... args) {
        std::lock_guard lock(m_mutex);
        m_queue.emplace(std::forward<Args>(args)...);
    }
    std::optional<T> try_pop() {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
        T val = std::move(m_queue.front());
        m_queue.pop();
        return val;
    }

   private:
    std::mutex m_mutex;
    std::queue<T> m_queue;
};

struct Resized {
    int width;
    int height;
};
struct ContentScale {
    float scale_factor;
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
    ConQueue<Resized> resized;
    ConQueue<ContentScale> content_scale;
    ConQueue<KeyInput> key_input;
    ConQueue<CursorPos> cursor_pos;
    ConQueue<CursorEnter> cursor_enter;
    ConQueue<MouseButton> mouse_button;
    ConQueue<Scroll> scroll;
    ConQueue<PathDrop> drops;
    ConQueue<ReceivedCharacter> received_character;
    ConQueue<bool> focused;
    ConQueue<std::pair<int, int>> moved;
};

int map_key_to_glfw(input::KeyCode key);
int map_mouse_button_to_glfw(input::MouseButton button);
input::KeyCode map_glfw_key_to_input(int key);
input::MouseButton map_glfw_mouse_button_to_input(int button);
/** @brief Resource mapping entity IDs to their native GLFW window
 * pointers. */
EPIX_EXPORT struct GLFWwindows : public std::unordered_map<epix::ecs::Entity, GLFWwindow*> {};

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
    static void update(epix::ecs::ResMut<Clipboard> clipboard);
    /** @brief System that writes pending clipboard text to the OS. */
    static void set_text(epix::ecs::EventReader<SetClipboardString> events);
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
EPIX_EXPORT struct GLFWRunner : public epix::app::AppRunner {
   public:
    GLFWRunner(epix::app::App& app);
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

   private:
    std::unique_ptr<epix::ecs::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<epix::ecs::System<std::tuple<>, void>> remove_window;
    epix::ecs::FilteredAccessSet exit_access;
    epix::ecs::FilteredAccessSet remove_access;
    std::unique_ptr<epix::ecs::System<std::tuple<>, void>> create_windows_system, update_size_system, update_pos_system,
        toggle_window_mode_system, update_window_states_system, destroy_windows_system, send_cached_events_system,
        clipboard_set_text_system, clipboard_update_system;
    std::vector<std::unique_ptr<epix::ecs::System<std::tuple<>, void>>> extra_systems;
    std::optional<std::future<std::unique_ptr<epix::app::App>>> render_app_future;
    std::optional<epix::app::AppLabel> render_app_label;
};
/** @brief Plugin that registers the GLFW windowing backend, including
 * window creation, event dispatch, and lifecycle systems. */
EPIX_EXPORT struct GLFWPlugin {
    void attach(epix::app::App& app);
    void detach(epix::app::App& app);

    /** @brief System that syncs window size from GLFW to the Window component. */
    static void update_size(
        epix::ecs::Query<
            epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::ecs::ResMut<GLFWwindows> glfw_windows);
    /** @brief System that syncs window position from GLFW. */
    static void update_pos(
        epix::ecs::Commands commands,
        epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                         epix::ecs::Mut<window::Window>,
                                         epix::ecs::Opt<const window::CachedWindow&>,
                                         epix::ecs::Opt<const epix::ecs::Parent&>>> windows,
        epix::ecs::ResMut<GLFWwindows> glfw_windows,
        epix::ecs::Local<std::unordered_map<epix::ecs::Entity, std::pair<int, int>>> pending_window_positions);
    /** @brief System that creates native GLFW windows for new Window entities. */
    static void create_windows(
        epix::ecs::Commands cmd,
        epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                         epix::ecs::Mut<window::Window>,
                                         epix::ecs::Opt<epix::ecs::Ref<epix::ecs::Parent>>,
                                         epix::ecs::Opt<epix::ecs::Ref<epix::ecs::Children>>>> windows,
        epix::ecs::ResMut<GLFWwindows> glfw_windows,
        epix::ecs::EventWriter<window::WindowCreated> window_created);
    /** @brief System that applies window state changes (title, cursor, icon, etc.). */
    static void update_window_states(
        epix::ecs::Query<
            epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::ecs::Res<assets::Assets<image::Image>> images,
        epix::ecs::ResMut<GLFWwindows> glfw_windows);
    /** @brief System that toggles between windowed and fullscreen modes. */
    static void toggle_window_mode(
        epix::ecs::Query<
            epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<window::Window>, const window::CachedWindow&>> windows,
        epix::ecs::ResMut<GLFWwindows> glfw_windows,
        epix::ecs::Local<std::unordered_map<epix::ecs::Entity, CachedWindowPosSize>> cached_window_sizes);
    /** @brief Poll all pending GLFW events. */
    static void poll_events();
    /** @brief System that dispatches cached GLFW events to the ECS event system. */
    static void send_cached_events(epix::ecs::Query<epix::ecs::Item<const window::CachedWindow&>> cached_windows,
                                   epix::ecs::ResMut<GLFWwindows> glfw_windows,
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
    /** @brief System that destroys GLFW windows for removed Window entities. */
    static void destroy_windows(epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const window::Window&>> windows,
                                epix::ecs::ResMut<GLFWwindows> glfw_windows,
                                epix::ecs::EventWriter<window::WindowClosed> window_closed,
                                epix::ecs::EventWriter<window::WindowDestroyed> window_destroyed);

   private:
    static GLFWwindow* create_window(epix::ecs::Entity id, window::Window& window);
};
}  // namespace epix::glfw
