export module epix.window:events;

import epix.core;

export namespace window {
/**
 * @brief This event is sent when the window is resized.
 */
struct WindowResized {
    core::Entity window;
    int width;
    int height;
};
/**
 * @brief This event is sent when the internal window is created.
 */
struct WindowCreated {
    core::Entity window;
};
/**
 * @brief This event is sent when the window is closed.
 *
 * Sent when the entity is despawned or loses its window component.
 */
struct WindowClosed {
    core::Entity window;
};
/**
 * @brief This event is sent when the os requests the window to be closed.
 */
struct WindowCloseRequested {
    core::Entity window;
};
/**
 * @brief This event is sent when the internal window is destroyed.
 */
struct WindowDestroyed {
    core::Entity window;
};
/** @brief Event sent when the cursor position changes within a window. */
struct CursorMoved {
    /** @brief The window entity the cursor moved in. */
    core::Entity window;
    /** @brief Current cursor position (x, y) in window coordinates. */
    std::pair<double, double> position;
    /** @brief Change in cursor position since the last event. */
    std::pair<double, double> delta;
};
/** @brief Event sent when the cursor enters or leaves a window. */
struct CursorEntered {
    /** @brief The window entity the cursor entered or left. */
    core::Entity window;
    /** @brief True if the cursor entered, false if it left. */
    bool entered;
};
/** @brief Event sent when a Unicode character is input to a window. */
struct ReceivedCharacter {
    /** @brief The window entity that received the character. */
    core::Entity window;
    /** @brief The Unicode code point of the received character. */
    char32_t character;
};
/** @brief Event sent when a window gains or loses focus. */
struct WindowFocused {
    /** @brief The window entity whose focus state changed. */
    core::Entity window;
    /** @brief True if the window gained focus, false if it lost focus. */
    bool focused;
};
/** @brief Event sent when files are dropped onto a window. */
struct FileDrop {
    /** @brief The window entity that received the file drop. */
    core::Entity window;
    /** @brief File system paths of the dropped files. */
    std::vector<std::string> paths;
};
/** @brief Event sent when a window is moved to a new screen position. */
struct WindowMoved {
    /** @brief The window entity that was moved. */
    core::Entity window;
    /** @brief New position (x, y) of the window on screen. */
    std::pair<int, int> position;
};
}  // namespace window