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
struct CursorMoved {
    core::Entity window;
    std::pair<double, double> position;
    std::pair<double, double> delta;
};
struct CursorEntered {
    core::Entity window;
    bool entered;
};
struct ReceivedCharacter {
    core::Entity window;
    char32_t character;
};
struct WindowFocused {
    core::Entity window;
    bool focused;
};
struct FileDrop {
    core::Entity window;
    std::vector<std::string> paths;
};
struct WindowMoved {
    core::Entity window;
    std::pair<int, int> position;
};
}  // namespace window