module;
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#endif

export module epix.window:structs;

import epix.core;
import epix.assets;
import epix.image;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export namespace epix::window {
/** @brief Controls how the window position is interpreted. */
enum PosType {
    /** @brief Position is relative to the top-left corner of the current
     * monitor. */
    TopLeft,
    /** @brief Window is centered on the current monitor. */
    Centered,
    /** @brief Position is relative to the parent window, top-left
     * aligned. */
    Relative,
};
/** @brief Pixel sizes of the window frame (borders and title bar). */
struct FrameSize {
    int left   = 0;
    int right  = 0;
    int top    = 0;
    int bottom = 0;
};
/** @brief Minimum and maximum size constraints for a window. A value of -1
 * means no limit for the corresponding maximum dimension. */
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
/** @brief Standard system cursor shapes. */
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
/** @brief Controls cursor visibility and confinement behaviour. */
enum class CursorMode {
    /** @brief Cursor is visible and free to move. */
    Normal,
    /** @brief Cursor is hidden but still tracks position. */
    Hidden,
    /** @brief Cursor is hidden and confined to the window. */
    Captured,
    /** @brief Cursor is hidden and virtual; provides unbounded motion
     * (useful for FPS cameras). */
    Disabled,
};
/** @brief Custom cursor using an image asset with a hotspot position. */
struct CustomCursor {
    /** @brief Image asset used as the cursor icon. */
    assets::Handle<image::Image> image;
    /** @brief X coordinate of the cursor hotspot within the image. */
    std::uint32_t hot_x = 0;
    /** @brief Y coordinate of the cursor hotspot within the image. */
    std::uint32_t hot_y = 0;

    bool operator==(const CustomCursor& other) const {
        return image == other.image && hot_x == other.hot_x && hot_y == other.hot_y;
    }
    bool operator!=(const CustomCursor& other) const { return !(*this == other); }
};
/** @brief Cursor icon variant: either a StandardCursor or a CustomCursor.
 */
struct CursorIcon : std::variant<StandardCursor, CustomCursor> {
    using std::variant<StandardCursor, CustomCursor>::variant;
    using std::variant<StandardCursor, CustomCursor>::operator=;
};
/** @brief Alpha compositing mode for the window surface. */
enum class CompositeAlphaMode {
    Auto,
    Opacity,
    PreMultiplied,
    PostMultiplied,
    Inherit,
};
/** @brief Presentation mode controlling vsync and latency. */
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
/** @brief Z-ordering level of the window relative to other windows. */
enum class WindowLevel {
    AlwaysOnBottom,
    Normal,
    AlwaysOnTop,
};
/** @brief Fullscreen or windowed display mode. */
enum class WindowMode {
    Windowed,
    Fullscreen,
    BorderlessFullscreen,
};
/** @brief Main window configuration component.
 *
 * Controls position, size, cursor, decorations, fullscreen mode, and
 * other OS-level window properties. Attach to an entity to create a
 * native window through a windowing backend (GLFW or SFML).
 */
struct Window {
    /** @brief How the position is interpreted. Changing this recalculates
     * the final position based on the current monitor and size. */
    PosType pos_type = PosType::Centered;
    /** @brief Requested position, interpreted according to pos_type. */
    std::pair<int, int> pos = {0, 0};
    /** @brief Actual screen position. Writing this takes priority over `pos`.
     * Usually equals `pos` when pos_type is TopLeft and monitor is 0. */
    std::pair<int, int> final_pos = {0, 0};

    /** @brief Size of the window in pixels. */
    std::pair<int, int> size = {1280, 720};

    /** @brief Cursor position (x, y) in client-area coordinates.
     *
     * Origin is the top-left corner of the window client area.
     * +x points right, +y points down.
     *
     * In Normal/Hidden/Captured modes, when the cursor is inside the window,
     * values are typically in [0, width) x [0, height). Values may be outside
     * that range when the cursor leaves the window.
     *
     * In Disabled mode, this is a virtual unbounded cursor position and is not
     * clamped to window bounds.
     *
     * This field is runtime state and is ignored at window creation.
     */
    std::pair<double, double> cursor_pos = {0.0, 0.0};
    /** @brief Whether the cursor is inside the window (read-only). */
    bool cursor_in_window = false;

    /** @brief Frame border sizes in pixels (read-only). */
    FrameSize frame_size = {};

    /** @brief Window size constraints.
     *
     * Note that even if limits not set, there can still be implicit limits based on backend.
     */
    SizeLimits size_limits = {};

    /** @brief Whether the window is resizable by the user. */
    bool resizable = true;
    /** @brief Whether the window has a title bar and borders. */
    bool decorations = true;
    /** @brief Whether the window is visible on screen. */
    bool visible = true;
    /** @brief Window opacity (0.0 = fully transparent, 1.0 = fully opaque). */
    float opacity = 1.0f;

    /** @brief Whether the window currently has input focus (read-only). */
    bool focused = false;
    /** @brief Whether the window is minimized (read-only). */
    bool iconified = false;
    /** @brief Whether the window is maximized (read-only). */
    bool maximized = false;

    /** @brief Optional window icon image. */
    std::optional<assets::Handle<image::Image>> icon;

    /** @brief Cursor icon (standard shape or custom image). */
    CursorIcon cursor_icon = StandardCursor::Arrow;
    /** @brief Cursor visibility and confinement mode. */
    CursorMode cursor_mode = CursorMode::Normal;

    /** @brief Alpha compositing mode for the window surface. */
    CompositeAlphaMode composite_alpha_mode = CompositeAlphaMode::Auto;
    /** @brief Presentation/vsync mode. */
    PresentMode present_mode = PresentMode::AutoNoVsync;

    /** @brief Z-ordering level relative to other windows. */
    WindowLevel window_level = WindowLevel::Normal;
    /** @brief Fullscreen or windowed mode. */
    WindowMode window_mode = WindowMode::Windowed;
    /** @brief Monitor index for fullscreen windows. */
    int monitor = 0;

    /** @brief Title displayed in the window title bar. */
    std::string title = "untitled";

    /** @brief Whether an attention request (taskbar flash) is pending. */
    bool attention_request = false;

    /** @brief Request user attention (e.g. taskbar flash). */
    void request_attention(bool request = true) { attention_request = request; }

    /** @brief Get a relative cursor position to the window's current position and size.
     *
     * Origin will be center of the window, +x right, +y up.
     * Values will be [-0.5, 0.5] when the cursor is within the window bounds.
     */
    std::pair<double, double> relative_cursor_pos() const {
        double rel_x = (cursor_pos.first / size.first) - 0.5;
        double rel_y = 0.5 - (cursor_pos.second / size.second);
        return {rel_x, rel_y};
    }
};

/** @brief Marker component designating the primary (main) window entity.
 */
struct PrimaryWindow {};
}  // namespace epix::window

namespace epix::window {
struct CachedWindowMut : Window {};  // component for caching window state.
/** @brief Read-only cached snapshot of a window's state from the previous
 * frame.
 *
 * Users can only access CachedWindow as `const` to detect state changes
 * between frames.
 */
export using CachedWindow = const CachedWindowMut;  // user can only access cached window as const.
}  // namespace epix::window