#pragma once

#include <epix/app.h>

namespace epix::window::events {
/**
 * @brief This event is sent when the window is resized.
 */
struct WindowResized {
    Entity window;
    int width;
    int height;
};
/**
 * @brief This event is sent when the internal window is created.
 */
struct WindowCreated {
    Entity window;
};
/**
 * @brief This event is sent when the window is closed.
 *
 * Sent when the entity is despawned or loses its window component.
 */
struct WindowClosed {
    Entity window;
};
/**
 * @brief This event is sent when the os requests the window to be closed.
 */
struct WindowCloseRequested {
    Entity window;
};
/**
 * @brief This event is sent when the internal window is destroyed.
 */
struct WindowDestroyed {
    Entity window;
};
struct CursorMoved {
    Entity window;
    std::pair<double, double> position;
    std::pair<double, double> delta;
};
struct CursorEntered {
    Entity window;
    bool entered;
};
struct ReceivedCharacter {
    Entity window;
    char32_t character;
};
struct WindowFocused {
    Entity window;
    bool focused;
};
struct FileDrop {
    Entity window;
    std::vector<std::string> paths;
};
struct WindowMoved {
    Entity window;
    std::pair<int, int> position;
};
}  // namespace epix::window::events