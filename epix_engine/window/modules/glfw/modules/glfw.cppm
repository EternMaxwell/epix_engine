module;

#include <GLFW/glfw3.h>

export module epix.glfw.core;

import epix.core;
import epix.input;
import epix.window;
import epix.assets;

using namespace core;

namespace glfw {
struct SetCustomCursor {
    Entity window;
    assets::UntypedHandle image;
};
struct PresentModeChange {
    Entity window;
    window::PresentMode present_mode;
};
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
export struct PathDrop {
    std::vector<std::string> paths;
};
export struct ReceivedCharacter {
    char32_t character;
};
/**
 * @brief User data for the glfw window.
 *
 * This will include cached events for later update the event queue in app.
 */
struct UserData {
    std::vector<SetCustomCursor> set_custom_cursor;
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

export struct GLFWwindows : public std::unordered_map<Entity, std::pair<GLFWwindow*, window::Window>> {};

export struct SetClipboardString {
    std::string text;
};
export struct Clipboard {
   private:
    std::string text;

   public:
    const std::string& get_text() const;
    static void update(ResMut<Clipboard> clipboard);
    static void set_text(EventReader<SetClipboardString> events);
};
struct CachedWindowPosSize {
    int pos_x  = 0;
    int pos_y  = 0;
    int width  = 0;
    int height = 0;
};
struct GLFWRunner : public AppRunner {
    std::unique_ptr<core::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<core::System<std::tuple<>, void>> remove_window;
    core::FilteredAccessSet exit_access;
    core::FilteredAccessSet remove_access;
    std::unique_ptr<core::System<std::tuple<>, void>> create_windows_system, update_size_system, update_pos_system,
        toggle_window_mode_system, update_window_states_system, destroy_windows_system, send_cached_events_system,
        clipboard_set_text_system, clipboard_update_system;
    std::optional<std::future<bool>> render_app_future;
    GLFWRunner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;
};
export struct GLFWPlugin {
    void build(App& app);

    static void update_size(Query<Item<Entity, Mut<window::Window>>> windows, ResMut<GLFWwindows> glfw_windows);
    static void update_pos(Commands commands,
                           Query<Item<Entity, Mut<window::Window>, Opt<const Parent&>>> windows,
                           ResMut<GLFWwindows> glfw_windows);
    static void create_windows(Commands commands,
                               Query<Item<Entity, Mut<window::Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                               ResMut<GLFWwindows> glfw_windows,
                               EventWriter<window::WindowCreated> window_created);
    static void update_window_states(Query<Item<Entity, Mut<window::Window>>> windows,
                                     ResMut<GLFWwindows> glfw_windows,
                                     EventWriter<glfw::SetCustomCursor> set_custom_cursor);
    static void toggle_window_mode(Query<Item<Entity, Mut<window::Window>>> windows,
                                   ResMut<GLFWwindows> glfw_windows,
                                   Local<std::unordered_map<Entity, CachedWindowPosSize>> cached_window_sizes);
    static void poll_events();
    static void send_cached_events(ResMut<GLFWwindows> glfw_windows,
                                   EventWriter<SetCustomCursor> set_custom_cursor,
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
    static void destroy_windows(Query<Item<Entity, const window::Window&>> windows,
                                ResMut<GLFWwindows> glfw_windows,
                                EventWriter<window::WindowClosed> window_closed,
                                EventWriter<window::WindowDestroyed> window_destroyed);

   private:
    static GLFWwindow* create_window(Entity id, window::Window& window);
};
}  // namespace glfw