/**
 * @file epix.window.cppm
 * @brief C++20 module interface for the window management system.
 *
 * This module provides window creation and management functionality.
 */
module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module epix.window;

export import epix.core;
export import epix.input;
export import epix.assets;

export namespace epix::window {

/**
 * @brief Window configuration and state.
 */
struct Window {
    std::string title            = "Epix Window";
    uint32_t width               = 1280;
    uint32_t height              = 720;
    bool resizable               = true;
    bool decorated               = true;
    bool visible                 = true;
    bool focused                 = true;
    bool transparent             = false;
    std::optional<uint32_t> min_width;
    std::optional<uint32_t> min_height;
    std::optional<uint32_t> max_width;
    std::optional<uint32_t> max_height;
};

/**
 * @brief Condition for application exit.
 */
enum class ExitCondition {
    OnAllClosed,
    OnPrimaryClosed,
    None,
};

}  // namespace epix::window

export namespace epix::window::events {

/**
 * @brief Window resize event.
 */
struct WindowResized {
    epix::core::Entity window;
    uint32_t width;
    uint32_t height;
};

/**
 * @brief Window move event.
 */
struct WindowMoved {
    epix::core::Entity window;
    int32_t x;
    int32_t y;
};

/**
 * @brief Window created event.
 */
struct WindowCreated {
    epix::core::Entity window;
};

/**
 * @brief Window closed event.
 */
struct WindowClosed {
    epix::core::Entity window;
};

/**
 * @brief Window close requested event.
 */
struct WindowCloseRequested {
    epix::core::Entity window;
};

/**
 * @brief Window destroyed event.
 */
struct WindowDestroyed {
    epix::core::Entity window;
};

/**
 * @brief Cursor moved event.
 */
struct CursorMoved {
    epix::core::Entity window;
    double x;
    double y;
};

/**
 * @brief Cursor entered/left window event.
 */
struct CursorEntered {
    epix::core::Entity window;
    bool entered;
};

/**
 * @brief File drop event.
 */
struct FileDrop {
    epix::core::Entity window;
    std::vector<std::string> paths;
};

/**
 * @brief Character input event.
 */
struct ReceivedCharacter {
    epix::core::Entity window;
    uint32_t character;
};

/**
 * @brief Window focus event.
 */
struct WindowFocused {
    epix::core::Entity window;
    bool focused;
};

}  // namespace epix::window::events

export namespace epix::window {

using namespace events;

/**
 * @brief Plugin for window management.
 */
struct WindowPlugin {
    std::optional<Window> primary_window = Window{};
    ExitCondition exit_condition         = ExitCondition::OnPrimaryClosed;
    bool close_when_requested            = true;
    void build(epix::core::App& app);
    void finish(epix::core::App& app);
};

}  // namespace epix::window
