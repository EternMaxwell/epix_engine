module;

#include <GLFW/glfw3.h>

export module epix.glfw.core;

import epix.core;
import epix.input;
import epix.window;
import epix.assets;
import epix.image;

using namespace core;

namespace glfw {
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
export struct PathDrop {
    /** @brief File system paths of the dropped files. */
    std::vector<std::string> paths;
};
/** @brief Event carrying a Unicode character received by a GLFW window. */
export struct ReceivedCharacter {
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
export struct GLFWwindows : public std::unordered_map<Entity, GLFWwindow*> {};

/** @brief Event requesting the clipboard text to be set. */
export struct SetClipboardString {
    /** @brief The text to write to the clipboard. */
    std::string text;
};
/** @brief Resource providing read access to the system clipboard. */
export struct Clipboard {
   private:
    std::string text;

   public:
    /** @brief Get the current clipboard text. */
    const std::string& get_text() const;
    /** @brief System that reads clipboard text from the OS. */
    static void update(ResMut<Clipboard> clipboard);
    /** @brief System that writes pending clipboard text to the OS. */
    static void set_text(EventReader<SetClipboardString> events);
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
export struct GLFWRunner : public AppRunner {
   public:
    GLFWRunner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;

    /** @brief Set the sub-app label used for rendering on a separate thread. */
    void set_render_app(const core::AppLabel& label) { render_app_label = label; }
    /** @brief Clear the render sub-app, running everything on the main thread. */
    void reset_render_app() { render_app_label = std::nullopt; }

    /** @brief Append an extra system to run each frame. */
    void append_system(std::unique_ptr<core::System<std::tuple<>, void>> system) {
        extra_systems.push_back(std::move(system));
    }

   private:
    std::unique_ptr<core::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<core::System<std::tuple<>, void>> remove_window;
    core::FilteredAccessSet exit_access;
    core::FilteredAccessSet remove_access;
    std::unique_ptr<core::System<std::tuple<>, void>> create_windows_system, update_size_system, update_pos_system,
        toggle_window_mode_system, update_window_states_system, destroy_windows_system, send_cached_events_system,
        clipboard_set_text_system, clipboard_update_system;
    std::vector<std::unique_ptr<core::System<std::tuple<>, void>>> extra_systems;
    std::optional<std::future<bool>> render_app_future;
    std::optional<core::AppLabel> render_app_label;
};
/** @brief Plugin that registers the GLFW windowing backend, including
 * window creation, event dispatch, and lifecycle systems. */
export struct GLFWPlugin {
    void build(App& app);

    /** @brief System that syncs window size from GLFW to the Window component. */
    static void update_size(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                            ResMut<GLFWwindows> glfw_windows);
    /** @brief System that syncs window position from GLFW. */
    static void update_pos(
        Commands commands,
        Query<Item<Entity, Mut<window::Window>, Opt<const window::CachedWindow&>, Opt<const Parent&>>> windows,
        ResMut<GLFWwindows> glfw_windows);
    /** @brief System that creates native GLFW windows for new Window entities. */
    static void create_windows(Commands cmd,
                               Query<Item<Entity, Mut<window::Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                               ResMut<GLFWwindows> glfw_windows,
                               EventWriter<window::WindowCreated> window_created);
    /** @brief System that applies window state changes (title, cursor, icon, etc.). */
    static void update_window_states(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                                     Res<assets::Assets<image::Image>> images,
                                     ResMut<GLFWwindows> glfw_windows);
    /** @brief System that toggles between windowed and fullscreen modes. */
    static void toggle_window_mode(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                                   ResMut<GLFWwindows> glfw_windows,
                                   Local<std::unordered_map<Entity, CachedWindowPosSize>> cached_window_sizes);
    /** @brief Poll all pending GLFW events. */
    static void poll_events();
    /** @brief System that dispatches cached GLFW events to the ECS event system. */
    static void send_cached_events(Query<Item<const window::CachedWindow&>> cached_windows,
                                   ResMut<GLFWwindows> glfw_windows,
                                   EventWriter<window::WindowResized> window_resized,
                                   EventWriter<window::WindowCloseRequested> window_close_requested,
                                   EventWriter<window::CursorMoved> cursor_moved,
                                   EventWriter<window::CursorEntered> cursor_entered,
                                   EventWriter<window::FileDrop> file_drop,
                                   EventWriter<window::ReceivedCharacter> received_character,
                                   EventWriter<window::WindowFocused> window_focused,
                                   EventWriter<window::WindowMoved> window_moved,
                                   std::optional<EventWriter<input::KeyInput>> key_input,
                                   std::optional<EventWriter<input::MouseButtonInput>> mouse_button_input,
                                   std::optional<EventWriter<input::MouseMove>> mouse_move_input,
                                   std::optional<EventWriter<input::MouseScroll>> scroll_input);
    /** @brief System that destroys GLFW windows for removed Window entities. */
    static void destroy_windows(Query<Item<Entity, const window::Window&>> windows,
                                ResMut<GLFWwindows> glfw_windows,
                                EventWriter<window::WindowClosed> window_closed,
                                EventWriter<window::WindowDestroyed> window_destroyed);

   private:
    static GLFWwindow* create_window(Entity id, window::Window& window);
};
}  // namespace glfw