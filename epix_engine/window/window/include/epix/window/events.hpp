#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <string>
#include <utility>
#include <vector>
#endif

EPIX_EXPORT namespace epix::window {
    /**
     * @brief This event is sent when the window is resized.
     */
    struct WindowResized {
        epix::ecs::Entity window;
        int width;
        int height;
    };
    /**
     * @brief This event is sent when the internal window is created.
     */
    struct WindowCreated {
        epix::ecs::Entity window;
    };
    /**
     * @brief This event is sent when the window is closed.
     *
     * Sent when the entity is despawned or loses its window component.
     */
    struct WindowClosed {
        epix::ecs::Entity window;
    };
    /**
     * @brief This event is sent when the os requests the window to be closed.
     */
    struct WindowCloseRequested {
        epix::ecs::Entity window;
    };
    /**
     * @brief This event is sent when the internal window is destroyed.
     */
    struct WindowDestroyed {
        epix::ecs::Entity window;
    };
    /** @brief Event sent when the cursor position changes for a window. */
    struct CursorMoved {
        /** @brief The window entity the cursor moved in. */
        epix::ecs::Entity window;
        /** @brief Current cursor position (x, y) in client-area coordinates.
         *
         * Origin is top-left; +x right, +y down. In Disabled cursor mode this can
         * be virtual/unbounded.
         */
        std::pair<double, double> position;
        /** @brief Delta (dx, dy) since the previous cursor event in the same coordinates. */
        std::pair<double, double> delta;
    };
    /** @brief Event sent when the cursor enters or leaves a window. */
    struct CursorEntered {
        /** @brief The window entity the cursor entered or left. */
        epix::ecs::Entity window;
        /** @brief True if the cursor entered, false if it left. */
        bool entered;
    };
    /** @brief Event sent when a Unicode character is input to a window. */
    struct ReceivedCharacter {
        /** @brief The window entity that received the character. */
        epix::ecs::Entity window;
        /** @brief The Unicode code point of the received character. */
        char32_t character;
    };
    /** @brief Event sent when a window gains or loses focus. */
    struct WindowFocused {
        /** @brief The window entity whose focus state changed. */
        epix::ecs::Entity window;
        /** @brief True if the window gained focus, false if it lost focus. */
        bool focused;
    };
    /** @brief Event sent when files are dropped onto a window. */
    struct FileDrop {
        /** @brief The window entity that received the file drop. */
        epix::ecs::Entity window;
        /** @brief File system paths of the dropped files. */
        std::vector<std::string> paths;
    };
    /** @brief Event sent when a window is moved to a new screen position. */
    struct WindowMoved {
        /** @brief The window entity that was moved. */
        epix::ecs::Entity window;
        /** @brief New position (x, y) of the window on screen. */
        std::pair<int, int> position;
    };
}  // namespace epix::window