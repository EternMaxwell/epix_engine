module;

#include <SFML/Window.hpp>

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

export struct SFMLwindows : public std::unordered_map<Entity, std::unique_ptr<sf::Window>> {};

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
export struct SFMLRunner : public AppRunner {
   public:
    SFMLRunner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;

   private:
    std::unique_ptr<core::System<std::tuple<>, std::optional<int>>> check_exit;
    std::unique_ptr<core::System<std::tuple<>, void>> remove_window;
    core::FilteredAccessSet exit_access;
    core::FilteredAccessSet remove_access;
    std::unique_ptr<core::System<std::tuple<>, void>> create_windows_system, update_size_system, update_pos_system,
        toggle_window_mode_system, update_window_states_system, destroy_windows_system, send_cached_events_system,
        clipboard_set_text_system, clipboard_update_system;
};
export struct SFMLPlugin {
    void build(App& app);

    static void update_size(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                            ResMut<SFMLwindows> sfml_windows);
    static void update_pos(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                           ResMut<SFMLwindows> sfml_windows);
    static void create_windows(Commands cmd,
                               Query<Item<Entity, Mut<window::Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                               ResMut<SFMLwindows> sfml_windows,
                               EventWriter<window::WindowCreated> window_created);
    static void update_window_states(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                                     Res<assets::Assets<image::Image>> images,
                                     ResMut<SFMLwindows> sfml_windows);
    static void toggle_window_mode(Query<Item<Entity, Mut<window::Window>, const window::CachedWindow&>> windows,
                                   ResMut<SFMLwindows> sfml_windows);
    static void poll_events();
    static void send_cached_events(Query<Item<const window::CachedWindow&>> cached_windows,
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
    static std::unique_ptr<sf::Window> create_window(Entity id, window::Window& window);
};
}  // namespace sfml
