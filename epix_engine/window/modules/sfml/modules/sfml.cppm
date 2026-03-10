module;

#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/WindowBase.hpp>
#include <SFML/Window/WindowEnums.hpp>
#include <SFML/Window/WindowHandle.hpp>

export module epix.sfml.core;

import epix.core;
import epix.input;
import epix.window;
import epix.assets;
import epix.image;

using namespace core;

namespace sfml {

input::KeyCode map_sfml_key_to_input(sf::Keyboard::Key key);
input::MouseButton map_sfml_mouse_button_to_input(sf::Mouse::Button button);

export struct SFMLwindows : public std::unordered_map<Entity, std::shared_ptr<sf::WindowBase>> {};

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
export struct SFMLRunner : public AppRunner {
   public:
    SFMLRunner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;

    void set_render_app(const core::AppLabel& label) { render_app_label = label; }
    void reset_render_app() { render_app_label = std::nullopt; }

    void append_system(std::unique_ptr<core::System<std::tuple<>, void>> system) {
        extra_systems.push_back(std::move(system));
    }

   private:
    std::unique_ptr<core::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<core::System<std::tuple<>, void>> remove_window;
    core::FilteredAccessSet exit_access;
    core::FilteredAccessSet remove_access;
    std::unique_ptr<core::System<std::tuple<>, void>> create_windows_system, update_size_system, update_pos_system,
        toggle_window_mode_system, update_window_states_system, destroy_windows_system, poll_and_send_events_system,
        clipboard_set_text_system, clipboard_update_system;
    std::vector<std::unique_ptr<core::System<std::tuple<>, void>>> extra_systems;
    std::optional<std::future<bool>> render_app_future;
    std::optional<core::AppLabel> render_app_label;
};
export struct SFMLPlugin {
    void build(App& app);

    static void update_size(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                            ResMut<SFMLwindows> sfml_windows);
    static void update_pos(
        Commands commands,
        Query<Item<Entity, Mut<window::Window>, Opt<const window::CachedWindow&>, Opt<const Parent&>>> windows,
        ResMut<SFMLwindows> sfml_windows,
        EventWriter<window::WindowMoved> window_moved);
    static void create_windows(Commands cmd,
                               Query<Item<Entity, Mut<window::Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                               ResMut<SFMLwindows> sfml_windows,
                               EventWriter<window::WindowCreated> window_created);
    static void update_window_states(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                                     Res<assets::Assets<image::Image>> images,
                                     ResMut<SFMLwindows> sfml_windows);
    static void toggle_window_mode(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                                   ResMut<SFMLwindows> sfml_windows,
                                   Local<std::unordered_map<Entity, CachedWindowPosSize>> cached_window_sizes);
    static void poll_and_send_events(Query<Item<const window::CachedWindow&>> cached_windows,
                                     ResMut<SFMLwindows> sfml_windows,
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
                                ResMut<SFMLwindows> sfml_windows,
                                EventWriter<window::WindowClosed> window_closed,
                                EventWriter<window::WindowDestroyed> window_destroyed);

   private:
    static std::shared_ptr<sf::WindowBase> create_window(Entity id, window::Window& window);
};
}  // namespace sfml
