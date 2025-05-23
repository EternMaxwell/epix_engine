#pragma once

#include <GLFW/glfw3.h>

#include "epix/app.h"
#include "epix/input.h"
#include "window/events.h"
#include "window/system.h"
#include "window/window.h"

namespace epix {
namespace window {
using window::Window;
struct Focus {
    std::optional<Entity> focus = std::nullopt;

    EPIX_API std::optional<Entity>& operator*();
    EPIX_API const std::optional<Entity>& operator*() const;
};
enum class ExitCondition {
    OnAllClosed,
    OnPrimaryClosed,
    None,
};
struct WindowPlugin : public app::Plugin {
    std::optional<Window> primary_window;
    ExitCondition exit_condition = ExitCondition::OnPrimaryClosed;
    bool close_when_requested    = true;
    EPIX_API void build(App& app) override;
};

EPIX_API void print_events(
    EventReader<events::WindowResized> resized,
    EventReader<events::WindowMoved> moved,
    EventReader<events::WindowCreated> created,
    EventReader<events::WindowClosed> closed,
    EventReader<events::WindowCloseRequested> close_requested,
    EventReader<events::WindowDestroyed> destroyed,
    EventReader<events::CursorMoved> cursor_moved,
    EventReader<events::CursorEntered> cursor_entered,
    EventReader<events::FileDrop> file_drop,
    EventReader<events::ReceivedCharacter> received_character,
    EventReader<events::WindowFocused> window_focused,
    Query<Get<window::Window>> windows
);

using events::CursorEntered;
using events::CursorMoved;
using events::FileDrop;
using events::ReceivedCharacter;
using events::WindowClosed;
using events::WindowCloseRequested;
using events::WindowCreated;
using events::WindowDestroyed;
using events::WindowFocused;
using events::WindowMoved;
using events::WindowResized;

using window::CompositeAlphaMode;
using window::Cursor;
using window::CursorIcon;
using window::CursorMode;
using window::PresentMode;
using window::StandardCursor;
using window::WindowFrameSize;
using window::WindowLevel;
using window::WindowMode;
using window::WindowPosition;
using window::WindowSize;
using window::WindowSizeLimit;
}  // namespace window
namespace glfw {
struct SetCustomCursor {
    Entity window;
    assets::UntypedHandle image;
};
struct PresentModeChange {
    Entity window;
    window::window::PresentMode present_mode;
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
struct PathDrop {
    std::vector<std::string> paths;
};
struct ReceivedCharacter {
    char32_t character;
};
/**
 * @brief User data for the glfw window.
 *
 * This will include cached events for later update the event queue in app.
 */
struct UserData {
    std::vector<SetCustomCursor> set_custom_cursor;
    epix::utils::async::ConQueue<Resized> resized;
    epix::utils::async::ConQueue<KeyInput> key_input;
    epix::utils::async::ConQueue<CursorPos> cursor_pos;
    epix::utils::async::ConQueue<CursorEnter> cursor_enter;
    epix::utils::async::ConQueue<MouseButton> mouse_button;
    epix::utils::async::ConQueue<Scroll> scroll;
    epix::utils::async::ConQueue<PathDrop> drops;
    epix::utils::async::ConQueue<ReceivedCharacter> received_character;
    epix::utils::async::ConQueue<bool> focused;
    epix::utils::async::ConQueue<std::pair<int, int>> moved;
};

EPIX_API int map_key_to_glfw(input::KeyCode key);
EPIX_API int map_mouse_button_to_glfw(input::MouseButton button);
EPIX_API input::KeyCode map_glfw_key_to_input(int key);
EPIX_API input::MouseButton map_glfw_mouse_button_to_input(int button);

struct GLFWwindows {
    entt::dense_map<Entity, GLFWwindow*> windows;
};

struct CachedWindows {
    entt::dense_map<Entity, window::Window> caches;
};

EPIX_API void sync_window_to_glfw(
    Entity id,
    window::Window& window_desc,
    window::Window& cached_desc,
    GLFWwindow* window
);
EPIX_API void sync_glfw_to_window(
    Entity id,
    window::Window& window_desc,
    window::Window& cached_desc,
    GLFWwindow* window
);
EPIX_API GLFWwindow* create_window(Entity id, window::Window& window_desc);

struct SetClipboardString {
    std::string text;
};
struct Clipboard {
   private:
    std::string text;

   public:
    EPIX_API const std::string& get_text() const;
    EPIX_API static void update(ResMut<Clipboard> clipboard);
    EPIX_API static void set_text(EventReader<SetClipboardString> events);
};
struct GLFWPlugin : public app::Plugin {
    EPIX_API void build(App& app) override;
    EPIX_API static void create_windows(
        Commands commands,
        Query<Get<Entity, window::window::Window>> windows,
        ResMut<GLFWwindows> glfw_windows,
        ResMut<CachedWindows> cached_windows,
        EventWriter<window::events::WindowCreated>& window_created
    );
    EPIX_API static void window_changed(
        Query<Get<Entity, window::window::Window>> windows,
        ResMut<GLFWwindows> glfw_windows,
        ResMut<CachedWindows> cached_windows
    );
    EPIX_API static void poll_events();
    EPIX_API static void sync_windows(
        Query<Get<Entity, window::window::Window>> windows,
        ResMut<GLFWwindows> glfw_windows,
        ResMut<CachedWindows> cached_windows
    );
    EPIX_API static void send_cached_events(
        ResMut<GLFWwindows> glfw_windows,
        ResMut<CachedWindows> cached_windows,
        EventWriter<SetCustomCursor>& set_custom_cursor,
        EventWriter<window::events::WindowResized>& window_resized,
        EventWriter<window::events::WindowCloseRequested>&
            window_close_requested,
        EventWriter<window::events::CursorMoved>& cursor_moved,
        EventWriter<window::events::CursorEntered>& cursor_entered,
        EventWriter<window::events::FileDrop>& file_drop,
        EventWriter<window::events::ReceivedCharacter>& received_character,
        EventWriter<window::events::WindowFocused>& window_focused,
        EventWriter<window::events::WindowMoved>& window_moved,
        std::optional<EventWriter<input::events::KeyInput>>& key_input,
        std::optional<EventWriter<input::events::MouseButtonInput>>&
            mouse_button_input,
        std::optional<EventWriter<input::events::MouseMove>>& mouse_move_input,
        std::optional<EventWriter<input::events::MouseScroll>>& scroll_input
    );
    EPIX_API static void destroy_windows(
        Query<Get<Entity, window::window::Window>> windows,
        ResMut<GLFWwindows> glfw_windows,
        ResMut<CachedWindows> cached_windows,
        EventWriter<window::events::WindowClosed>& window_closed,
        EventWriter<window::events::WindowDestroyed>& window_destroyed
    );
};
}  // namespace glfw
}  // namespace epix