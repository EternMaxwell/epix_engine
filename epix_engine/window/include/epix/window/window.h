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

    EPIX_API bool operator==(const Cursor& other) const;
};
enum class PresentMode {
    /**
     * @brief Chooses FifoRelaxed -> Fifo based on driver support.
     */
    AutoVsync,
    /**
     * @brief Chooses Immediate -> Mailbox -> Fifo based on driver support.
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

    EPIX_API bool operator==(const WindowPosition& other) const;
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

    EPIX_API bool operator==(const WindowSizeLimit& other) const;
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
enum class CompositeAlphaMode {
    Auto,
    Opacity,
    PreMultiplied,
    PostMultiplied,
    Inherit,
};
struct Window {
   public:
    std::optional<WindowPosition> position = std::nullopt;  // //
    WindowFrameSize frame_size;                             //
    WindowSize window_size = {};                            // //
    InternalState internal;                                 // //

    Cursor cursor                 = {};                        //
    PresentMode present_mode      = PresentMode::AutoNoVsync;  //
    WindowMode mode               = WindowMode::Windowed;      //
    int monitor                   = 0;
    WindowLevel window_level      = WindowLevel::Normal;  //
    std::string title             = "";                   //
    CompositeAlphaMode alpha_mode = CompositeAlphaMode::Auto;
    float opacity                 = 1.0f;   //
    WindowSizeLimit size_limit    = {};     //
    bool resizable                = true;   //
    bool decorations              = true;   //
    bool focused                  = false;  //
    bool visible                  = true;   //
    bool iconified                = false;  //

    EPIX_API std::pair<int, int> get_position() const;
    EPIX_API void set_position(int x, int y);
    EPIX_API const WindowFrameSize& get_frame_size() const;
    EPIX_API void set_maximized(bool maximized);
    EPIX_API int width() const;
    EPIX_API int height() const;
    EPIX_API std::pair<int, int> size() const;
    EPIX_API std::pair<int, int> physical_size() const;
    EPIX_API void set_size(int width, int height);
    /**
     * @brief Get the cursor position
     *
     * @return `std::tuple<bool, double, double>` with the `bool` indicating
     * whether cursor is in window, second and third the x and y position of the
     * cursor relavant to top left corner of the window.
     */
    EPIX_API std::tuple<bool, double, double> cursor_position() const;
    /**
     * @brief Get cursor position normalized (scaled to (0, 1)).
     */
    EPIX_API std::pair<double, double> cursor_position_normalized() const;
    EPIX_API void set_cursor_position(double x, double y);
};
}  // namespace epix::window::window
