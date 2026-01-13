export module epix.window:structs;

import epix.core;
import epix.assets;
import std;

export namespace window {
enum PosType {
    TopLeft,   // Top left corner of the current monitor
    Centered,  // Centered on the current monitor
    Relative,  // Relative to the parent window, top left aligned.
};
struct FrameSize {
    int left   = 0;
    int right  = 0;
    int top    = 0;
    int bottom = 0;
};
struct SizeLimits {
    int min_width  = 160;
    int min_height = 120;
    int max_width  = -1;  // -1 means no limit
    int max_height = -1;  // -1 means no limit

    bool operator==(const SizeLimits& other) const {
        return min_width == other.min_width && min_height == other.min_height && max_width == other.max_width &&
               max_height == other.max_height;
    }
    bool operator!=(const SizeLimits& other) const { return !(*this == other); }
};
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
struct CursorIcon : std::variant<StandardCursor, assets::UntypedHandle> {
    using std::variant<StandardCursor, assets::UntypedHandle>::variant;
    using std::variant<StandardCursor, assets::UntypedHandle>::operator=;
};
enum class CompositeAlphaMode {
    Auto,
    Opacity,
    PreMultiplied,
    PostMultiplied,
    Inherit,
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
enum class WindowLevel {
    AlwaysOnBottom,
    Normal,
    AlwaysOnTop,
};
enum class WindowMode {
    Windowed,
    Fullscreen,
    BorderlessFullscreen,
};
struct Window {
    // type of position. controls how the position is interpreted.
    // changing this value will change the final pos, and recalculate the
    // position based on the current monitor and size.
    PosType pos_type = PosType::Centered;
    // position of the window, final pos is calculated based on the pos_type.
    std::pair<int, int> pos = {0, 0};
    // the actual pos on the screen, changing this is prior to `pos`.
    // this is usually same as `pos` if type is `PosType::TopLeft` and `monitor`
    // is 0. This is usually irrelevent to monitor index.
    std::pair<int, int> final_pos = {0, 0};

    // size of the window.
    std::pair<int, int> size = {1280, 720};

    // cursor position relevant to the window, do nothing in creation.
    std::pair<double, double> cursor_pos = {0.0, 0.0};
    // whether cursor is in the window, get only.
    bool cursor_in_window = false;

    // frame size of the window, get only.
    FrameSize frame_size = {};

    // size limits.
    SizeLimits size_limits = {};

    // whether window is resizable.
    bool resizable = true;
    // whether window is decorated with a title bar and borders.
    bool decorations = true;
    // whether window is visible on the screen.
    bool visible = true;
    // opacity of the window, 0.0 is fully transparent, 1.0 is fully opaque.
    float opacity = 1.0f;

    // whether window is focused.
    bool focused = false;
    // whether window is iconified (minimized).
    bool iconified = false;
    // whether window is maximized.
    bool maximized = false;

    // cursor icon, can be standard or custom image.
    CursorIcon cursor_icon = StandardCursor::Arrow;
    // cursor mode, controls how the cursor is displayed.
    CursorMode cursor_mode = CursorMode::Normal;

    // composite alpha mode, used in rendering.
    CompositeAlphaMode composite_alpha_mode = CompositeAlphaMode::Auto;
    // present mode, controls how the rendered image is presented.
    PresentMode present_mode = PresentMode::AutoNoVsync;

    // window level, controls the z-order of the window.
    WindowLevel window_level = WindowLevel::Normal;
    // window mode, controls fullscreen.
    WindowMode window_mode = WindowMode::Windowed;
    // monitor index to use for fullscreen windows.
    int monitor = 0;

    // title of the window, displayed in the title bar.
    std::string title = "untitled";

    // whether request for attention.
    bool attention_request = false;

    void request_attention(bool request = true) { attention_request = request; }
};

struct PrimaryWindow {};
}  // namespace window