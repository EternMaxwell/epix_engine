#pragma once

#include <GLFW/glfw3.h>
#include <epix/assets.h>
#include <epix/utils/core.h>

#include <variant>

namespace epix::window::window {
enum class StandardCursor {
    Arrow,
    IBeam,
    Crosshair,
    Hand,
    ResizeAll,
    ResizeNS,
    ResizeEW,
    ResizeNWSE,
    ResizeNESW,
    NotAllowed,
};
enum class CursorMode {
    Normal,
    Hidden,
    Captured,
    Disabled,
};
struct CursorIcon : std::variant<StandardCursor, epix::assets::UntypedHandle> {
    using std::variant<StandardCursor, epix::assets::UntypedHandle>::variant;
    using std::variant<StandardCursor, epix::assets::UntypedHandle>::operator=;
};
struct Cursor {
    CursorIcon icon = StandardCursor::Arrow;
    CursorMode mode = CursorMode::Normal;

    bool operator==(const Cursor& other) const {
        return icon == other.icon && mode == other.mode;
    }
};
enum class PresentMode {
    /**
     * @brief Chooses FifoRelaxed -> Fifo based on driver support.
     */
    AutoVsync,
    /**
     * @brief Chooses Mailbox -> Immediate -> Fifo based on driver support.
     */
    AutoNoVsync,
    Fifo,
    FifoRelaxed,
    Immediate,
    Mailbox,
};
enum class WindowMode {
    Windowed,
    BorderlessFullscreen,
    Fullscreen,
};
struct WindowPosition {
    int x, y;

    bool operator==(const WindowPosition& other) const {
        return x == other.x && y == other.y;
    }
};
struct WindowSize {
    // framebuffer size
    int width  = 1280;
    int height = 720;
    // physical size
    int physical_width  = 1280;
    int physical_height = 720;
};
struct WindowSizeLimit {
    int min_width  = 180;
    int min_height = 120;
    int max_width  = -1;
    int max_height = -1;

    bool operator==(const WindowSizeLimit& other) const {
        return min_width == other.min_width && min_height == other.min_height &&
               max_width == other.max_width && max_height == other.max_height;
    }
};
enum class WindowLevel {
    AlwaysOnBottom,
    Normal,
    AlwaysOnTop,
};
struct InternalState {
    std::optional<bool> maximize_request;
    std::tuple<bool, double, double> cursor_position;
    std::optional<bool> attention_request;
};
struct WindowFrameSize {
    int left   = 0;
    int right  = 0;
    int top    = 0;
    int bottom = 0;
};
struct Window {
   public:
    std::optional<WindowPosition> position = std::nullopt;  // //
    WindowFrameSize frame_size;                             //
    WindowSize window_size = {};                            // //
    InternalState internal;                                 // //

    Cursor cursor              = {};                        //
    PresentMode present_mode   = PresentMode::AutoNoVsync;  //
    WindowMode mode            = WindowMode::Windowed;      //
    int monitor                = 0;
    WindowLevel window_level   = WindowLevel::Normal;  //
    std::string title          = "";                   //
    bool transparent           = false;                //
    float opacity              = 1.0f;                 //
    WindowSizeLimit size_limit = {};                   //
    bool resizable             = true;                 //
    bool decorations           = true;                 //
    bool focused               = false;                //
    bool visible               = true;                 //
    bool iconified            = false;                //

    std::pair<int, int> get_position() const {
        if (position) {
            return {position->x, position->y};
        }
        return {0, 0};
    }
    void set_position(int x, int y) {
        if (!position) {
            position = WindowPosition{x, y};
        } else {
            position->x = x;
            position->y = y;
        }
    }
    const WindowFrameSize& get_frame_size() const { return frame_size; }
    void set_maximized(bool maximized) {
        internal.maximize_request = maximized;
    }
    int width() const { return window_size.width; }
    int height() const { return window_size.height; }
    std::pair<int, int> size() const {
        return {window_size.width, window_size.height};
    }
    std::pair<int, int> physical_size() const {
        return {window_size.physical_width, window_size.physical_height};
    }
    void set_size(int width, int height) {
        window_size.physical_height = height;
        window_size.physical_width  = width;
    }
    /**
     * @brief Get the cursor position
     *
     * @return `std::tuple<bool, double, double>` with the `bool` indicating
     * whether cursor is in window, second and third the x and y position of the
     * cursor relavant to top left corner of the window.
     */
    std::tuple<bool, double, double> cursor_position() const {
        return internal.cursor_position;
    }
    /**
     * @brief Get cursor position normalized (scaled to (0, 1)).
     */
    std::pair<double, double> cursor_position_normalized() const {
        auto [_, posx, posy] = cursor_position();
        auto [width, height] = size();
        return {posx / width, posy / height};
    }
    void set_cursor_position(double x, double y) {
        auto& [_, posx, posy] = internal.cursor_position;
        posx                  = x;
        posy                  = y;
    }
};
}  // namespace epix::window::window
