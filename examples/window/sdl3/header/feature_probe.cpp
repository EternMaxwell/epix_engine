// Drives every field of `epix::window::Window` through the SDL3 backend and
// reports, for each one, what was requested and what was observed.
//
// Every step prints two independent observations:
//   * the engine's own `Window` component, which the backend writes back from
//     SDL each frame, and
//   * the raw native window state read straight from Win32 (styles, rect,
//     layered alpha, clip rectangle, cursor), so a claim never rests on the
//     engine agreeing with itself. Nothing here calls SDL.
//
// Output is line oriented and greppable:
//   PROBE|PASS <name> expected{...} observed{...}
//   PROBE|FAIL <name> expected{...} observed{...}
//   PROBE|OS <name> os{...}             raw native state when the check ran
//   PROBE|HB frame=.. t=..ms gap=..ms   periodic liveness heartbeat
//   PROBE|AUTO-COMPLETE passed=N failed=N
//
// After the automatic sequence the probe switches to an interactive phase where
// real OS input drives the window. Interactive controls:
//   F1 resizable  F2 decorations  F3 visible  F4 maximize  F5 minimize
//   F6 window level  F7 cursor mode  F8 cursor icon  F9 attention
//   F10 opacity  F11 window mode  F12 title  Space size  Escape close

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/window.hpp>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

using epix::assets::Assets;
using epix::assets::Handle;
using epix::ecs::Commands;
using epix::ecs::Entity;
using epix::image::Image;
using epix::window::Window;

std::uint64_t now_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

/** @brief Identifier of the calling thread, or 0 where it does not exist.
 *
 * Window introspection and cursor control are thread-affine on Windows: a
 * `SendMessage` from a worker thread to a window owned by the main thread
 * blocks until that thread pumps messages, which it cannot do while it is
 * waiting for the schedule that is running the worker. */
std::uint32_t current_thread_id() {
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
    return 0;
#endif
}

/** @brief The thread the process started on; windows are created on it. */
std::uint32_t main_thread_id = 0;

void emit(const std::string& line) { std::cout << "PROBE|" << line << std::endl; }

std::string yn(bool value) { return value ? "yes" : "no"; }
std::string num(long long value) { return std::to_string(value); }
std::string pair_str(int a, int b) { return "(" + num(a) + "," + num(b) + ")"; }
std::string rect_str(int a, int b, int c, int d) {
    return "(" + num(a) + "," + num(b) + " " + num(c) + "x" + num(d) + ")";
}
std::string size_str(const Window& w) { return num(w.size.first) + "x" + num(w.size.second); }
std::string pos_str(const Window& w) { return pair_str(w.final_pos.first, w.final_pos.second); }

std::string component_report(const Window& w) {    return "comp{title=\"" + w.title + "\" size=" + size_str(w) + " physical=" + num(w.physical_size.first) + "x" +
           num(w.physical_size.second) + " scale=" + std::to_string(w.scale_factor) + " final_pos=" + pos_str(w) +
           " cursor_pos=(" + num(static_cast<int>(w.cursor_pos.first)) + "," +
           num(static_cast<int>(w.cursor_pos.second)) + ") cursor_in_window=" + yn(w.cursor_in_window) + " frame=" +
           num(w.frame_size.left) + "/" + num(w.frame_size.right) + "/" + num(w.frame_size.top) + "/" +
           num(w.frame_size.bottom) + " focused=" + yn(w.focused) + " iconified=" + yn(w.iconified) +
           " maximized=" + yn(w.maximized) + " level=" + num(static_cast<int>(w.window_level)) +
           " mode=" + num(static_cast<int>(w.window_mode)) + "}";
}

#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)

/** @brief Find this process's SDL window without asking SDL for it. */
HWND find_own_window() {
    struct Search {
        DWORD pid;
        HWND sdl_app;
        HWND any;
    } search{GetCurrentProcessId(), nullptr, nullptr};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* search = reinterpret_cast<Search*>(param);
            DWORD pid    = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != search->pid) return TRUE;
            if (!IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
            char class_name[128] = {};
            GetClassNameA(hwnd, class_name, sizeof(class_name));
            if (std::strcmp(class_name, "SDL_app") == 0) {
                search->sdl_app = hwnd;
                return FALSE;
            }
            if (search->any == nullptr && std::strcmp(class_name, "ConsoleWindowClass") != 0) search->any = hwnd;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.sdl_app != nullptr ? search.sdl_app : search.any;
}

std::string os_title(HWND w) {
    char buffer[512] = {};
    GetWindowTextA(w, buffer, sizeof(buffer));
    return buffer;
}
bool os_visible(HWND w) { return IsWindowVisible(w) != 0; }
bool os_iconic(HWND w) { return IsIconic(w) != 0; }
bool os_zoomed(HWND w) { return IsZoomed(w) != 0; }
bool os_topmost(HWND w) { return (GetWindowLong(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; }
/** @brief True when the window carries a native frame (title bar / borders).
 *
 * This is measured from the geometry rather than from `WS_CAPTION`: SDL3's
 * default borderless-windowed style deliberately keeps `WS_CAPTION` while using
 * `WS_POPUP`, so the style bits alone do not say whether a frame is drawn. */
bool os_framed(HWND w) {
    RECT outer{};
    RECT client{};
    GetWindowRect(w, &outer);
    GetClientRect(w, &client);
    return (outer.right - outer.left) > (client.right - client.left) ||
           (outer.bottom - outer.top) > (client.bottom - client.top);
}
bool os_thickframe(HWND w) { return (GetWindowLong(w, GWL_STYLE) & WS_THICKFRAME) != 0; }
int os_alpha(HWND w) {
    if ((GetWindowLong(w, GWL_EXSTYLE) & WS_EX_LAYERED) == 0) return -1;
    DWORD key   = 0;
    BYTE alpha  = 0;
    DWORD flags = 0;
    if (!GetLayeredWindowAttributes(w, &key, &alpha, &flags)) return -1;
    return alpha;
}
void os_client_size(HWND w, int& width, int& height) {
    RECT rect{};
    GetClientRect(w, &rect);
    width  = rect.right - rect.left;
    height = rect.bottom - rect.top;
}
void os_client_origin(HWND w, int& x, int& y) {
    POINT point{0, 0};
    ClientToScreen(w, &point);
    x = point.x;
    y = point.y;
}
void os_monitor_size(HWND w, int& width, int& height) {
    MONITORINFO info{};
    info.cbSize   = sizeof(info);
    HMONITOR monitor = MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(monitor, &info)) {
        width  = info.rcMonitor.right - info.rcMonitor.left;
        height = info.rcMonitor.bottom - info.rcMonitor.top;
        return;
    }
    width  = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
}
void* os_cursor_handle() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    GetCursorInfo(&info);
    return static_cast<void*>(info.hCursor);
}
bool os_cursor_shown() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    GetCursorInfo(&info);
    return (info.flags & CURSOR_SHOWING) != 0;
}
void os_cursor_screen_pos(int& x, int& y) {
    POINT point{};
    GetCursorPos(&point);
    x = point.x;
    y = point.y;
}
/** @brief The current pointer clip rectangle, if the system has one. */
bool os_clip_rect(RECT& clip) { return GetClipCursor(&clip) != 0; }

/** @brief True when the pointer is confined inside the window's client area.
 *
 * SDL implements `CursorMode::Captured` and `CursorMode::Disabled` with a Win32
 * clip rectangle, so this observes the confinement directly rather than
 * trusting SDL's own flag. Relative mode locks the pointer to a small rectangle
 * at the client centre instead of the whole client area, so the test only asks
 * that the clip is inside the client area and smaller than the desktop. */
bool os_cursor_confined_to(HWND w) {
    RECT clip{};
    if (!os_clip_rect(clip)) return false;
    int x = 0;
    int y = 0;
    int cw = 0;
    int ch = 0;
    os_client_origin(w, x, y);
    os_client_size(w, cw, ch);
    const bool inside = clip.left >= x - 1 && clip.top >= y - 1 && clip.right <= x + cw + 1 &&
                        clip.bottom <= y + ch + 1;
    const bool smaller = (clip.right - clip.left) < GetSystemMetrics(SM_CXSCREEN);
    return inside && smaller;
}
void os_move_cursor_into(HWND w, int client_x, int client_y) {
    POINT point{client_x, client_y};
    ClientToScreen(w, &point);
    SetCursorPos(point.x, point.y);
}
void* os_window_icon(HWND w) {
    void* big = reinterpret_cast<void*>(SendMessageW(w, WM_GETICON, ICON_BIG, 0));
    if (big != nullptr) return big;
    return reinterpret_cast<void*>(GetClassLongPtrW(w, GCLP_HICON));
}
/** @brief Bring the window to the foreground so focus-dependent state applies. */
void os_force_foreground(HWND w) {
    const HWND foreground      = GetForegroundWindow();
    DWORD foreground_pid       = 0;
    const DWORD foreground_tid = GetWindowThreadProcessId(foreground, &foreground_pid);
    const DWORD this_tid       = GetCurrentThreadId();
    if (foreground_tid != this_tid) AttachThreadInput(this_tid, foreground_tid, TRUE);
    ShowWindow(w, SW_RESTORE);
    BringWindowToTop(w);
    SetForegroundWindow(w);
    if (foreground_tid != this_tid) AttachThreadInput(this_tid, foreground_tid, FALSE);
}

/** @brief Drive a real mouse button or wheel event through SendInput. */
void os_send_mouse(DWORD flag, DWORD data = 0) {
    INPUT input{};
    input.type       = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    input.mi.mouseData = data;
    SendInput(1, &input, sizeof(INPUT));
}

/** @brief Post a real WM_DROPFILES carrying every path, as Explorer would.
 *
 * The payload is the documented HDROP layout - a DWORD byte offset to the file
 * list, a POINT, and two BOOLs (20 bytes), then a double-NUL terminated list of
 * ANSI paths - laid out by hand so no shell header is needed. GHND zero-fills
 * everything except the offset. */
bool os_post_file_drop(HWND w, const std::vector<std::string>& paths) {
    constexpr std::size_t kHeaderBytes = 20;
    std::size_t bytes = 0;
    for (const auto& path : paths) bytes += path.size() + 1;
    HGLOBAL memory = GlobalAlloc(GHND, kHeaderBytes + bytes + 1);
    if (memory == nullptr) return false;
    auto* base = static_cast<std::byte*>(GlobalLock(memory));
    if (base == nullptr) {
        GlobalFree(memory);
        return false;
    }
    const auto offset = static_cast<std::uint32_t>(kHeaderBytes);
    std::memcpy(base, &offset, sizeof(offset));
    std::size_t cursor = kHeaderBytes;
    for (const auto& path : paths) {
        std::memcpy(base + cursor, path.c_str(), path.size());
        cursor += path.size();
        base[cursor++] = std::byte{0};
    }
    base[cursor] = std::byte{0};
    GlobalUnlock(memory);
    return PostMessageW(w, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0) != FALSE;
}

/** @brief The OS clipboard text, read straight from Win32. */
std::string os_get_clipboard_text() {
    if (OpenClipboard(nullptr) == FALSE) return {};
    std::string utf8;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT); handle != nullptr) {
        if (auto* wide = static_cast<const wchar_t*>(GlobalLock(handle)); wide != nullptr) {
            const int needed =
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (needed > 1) {
                utf8.resize(static_cast<std::size_t>(needed - 1));
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), needed, nullptr, nullptr);
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return utf8;
}

/** @brief Replace the OS clipboard text through Win32, as another app would. */
bool os_set_clipboard_text(const std::string& utf8) {
    if (OpenClipboard(nullptr) == FALSE) return false;
    EmptyClipboard();
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    HGLOBAL memory     = GlobalAlloc(GMEM_MOVEABLE, static_cast<std::size_t>(wide_len) * sizeof(wchar_t));
    if (memory == nullptr) {
        CloseClipboard();
        return false;
    }
    if (auto* wide = static_cast<wchar_t*>(GlobalLock(memory)); wide != nullptr) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide, wide_len);
        GlobalUnlock(memory);
    }
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return true;
}

std::string os_report(HWND w) {
    if (w == nullptr) return "os{no-hwnd}";
    RECT rect{};
    GetWindowRect(w, &rect);
    int client_w = 0;
    int client_h = 0;
    os_client_size(w, client_w, client_h);
    char cursor_handle[32] = {};
    std::snprintf(cursor_handle, sizeof(cursor_handle), "%p", os_cursor_handle());
    char icon_handle[32] = {};
    std::snprintf(icon_handle, sizeof(icon_handle), "%p", os_window_icon(w));
    RECT clip{};
    const bool has_clip    = os_clip_rect(clip);
    const std::string clip_str = has_clip ? rect_str(clip.left, clip.top, clip.right - clip.left,
                                                     clip.bottom - clip.top)
                                          : std::string("none");

    return "os{title=\"" + os_title(w) + "\" rect=" + rect_str(rect.left, rect.top, rect.right - rect.left,
                                                               rect.bottom - rect.top) +
           " client=" + num(client_w) + "x" + num(client_h) + " visible=" + yn(os_visible(w)) +
           " iconic=" + yn(os_iconic(w)) + " zoomed=" + yn(os_zoomed(w)) + " topmost=" + yn(os_topmost(w)) +
           " caption=" + yn(os_framed(w)) + " thickframe=" + yn(os_thickframe(w)) + " alpha=" + num(os_alpha(w)) +
           " cursor_shown=" + yn(os_cursor_shown()) + " cursor_confined=" + yn(os_cursor_confined_to(w)) +
           " clip=" + clip_str + " hcursor=0x" + cursor_handle + " hicon=0x" + icon_handle + " foreground=" +
           yn(GetForegroundWindow() == w) + "}";
}

#else

HWND find_own_window() { return nullptr; }
std::string os_report(HWND) { return "os{unavailable}"; }
void os_send_mouse(unsigned long, unsigned long = 0) {}
bool os_post_file_drop(HWND, const std::vector<std::string>&) { return false; }
std::string os_get_clipboard_text() { return {}; }
bool os_set_clipboard_text(const std::string&) { return false; }

#endif

struct Probe {
    int step                    = 0;
    std::uint64_t step_deadline = 0;
    bool applied                = false;
    std::uint64_t frame         = 0;
    std::uint64_t start         = 0;
    std::uint64_t last_hb       = 0;
    std::uint64_t last_frame_ms = 0;
    std::uint64_t max_gap_ms    = 0;
    int passed                  = 0;
    int failed                  = 0;
    bool automatic_done         = false;
    bool interactive            = false;
    int titles                  = 0;
    std::uint32_t main_thread   = 0;
    /** Bit i set once the engine reported `MouseButton(i)` pressed. */
    unsigned buttons_pressed    = 0;
    int scroll_events           = 0;
    int drop_events             = 0;
    int last_drop_paths         = 0;
    void* cursor_before         = nullptr;
    void* icon_before           = nullptr;
    std::optional<Handle<Image>> pattern;
};

struct Context {
    Commands& commands;
    Probe& probe;
    Entity window{};
    Window& win;
    HWND hwnd = nullptr;
    Assets<Image>& images;
    epix::window::Clipboard& clipboard;
};

struct Step {
    const char* name;
    std::uint64_t settle_ms;
    std::function<void(Context&)> apply;
    std::function<void(Context&)> check;
};

void check_int(Context& ctx, const char* name, const char* what, long long expected, long long observed) {
    const bool ok = expected == observed;
    emit(std::string(ok ? "PASS " : "FAIL ") + name + "." + what + " expected{" + num(expected) + "} observed{" +
         num(observed) + "}");
    ok ? ++ctx.probe.passed : ++ctx.probe.failed;
}
void check_str(Context& ctx, const char* name, const char* what, const std::string& expected,
               const std::string& observed) {
    const bool ok = expected == observed;
    emit(std::string(ok ? "PASS " : "FAIL ") + name + "." + what + " expected{" + expected + "} observed{" + observed +
         "}");
    ok ? ++ctx.probe.passed : ++ctx.probe.failed;
}
void check_bool(Context& ctx, const char* name, const char* what, bool expected, bool observed) {
    const bool ok = expected == observed;
    emit(std::string(ok ? "PASS " : "FAIL ") + name + "." + what + " expected{" + yn(expected) + "} observed{" +
         yn(observed) + "}");
    ok ? ++ctx.probe.passed : ++ctx.probe.failed;
}

constexpr std::uint64_t OS_SETTLE  = 300;
constexpr std::uint64_t ECS_SETTLE = 120;

std::vector<Step>& steps() {
    static std::vector<Step> table = [] {
        std::vector<Step> s;

        // 1. Creation defaults. -------------------------------------------------
        s.push_back({"create", OS_SETTLE, [](Context&) {}, [](Context& ctx) {
                         check_int(ctx, "create", "size.w", 640, ctx.win.size.first);
                         check_int(ctx, "create", "size.h", 480, ctx.win.size.second);
                         check_bool(ctx, "create", "physical_size_nonzero", true,
                                    ctx.win.physical_size.first > 0 && ctx.win.physical_size.second > 0);
                         check_bool(ctx, "create", "scale_factor_positive", true, ctx.win.scale_factor > 0.0f);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_str(ctx, "create", "os_title", "Epix SDL3 Feature Probe", os_title(ctx.hwnd));
                         check_bool(ctx, "create", "os_framed", true, os_framed(ctx.hwnd));
                         check_bool(ctx, "create", "os_thickframe", true, os_thickframe(ctx.hwnd));
                         check_bool(ctx, "create", "os_visible", true, os_visible(ctx.hwnd));
                         check_bool(ctx, "create", "frame_size_top_positive", true, ctx.win.frame_size.top > 0);
                         check_str(ctx, "create", "os_client_origin", "(80,80)", [&] {
                             int x = 0;
                             int y = 0;
                             os_client_origin(ctx.hwnd, x, y);
                             return pair_str(x, y);
                         }());
#endif
                     }});

        // 2. Title. ------------------------------------------------------------
        s.push_back({"title", OS_SETTLE,
                     [](Context& ctx) { ctx.win.title = "Epix SDL3 Feature Probe (renamed)"; },
                     [](Context& ctx) {
                         check_str(ctx, "title", "component", "Epix SDL3 Feature Probe (renamed)", ctx.win.title);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_str(ctx, "title", "os", "Epix SDL3 Feature Probe (renamed)", os_title(ctx.hwnd));
#endif
                     }});

        // 3. Size. -------------------------------------------------------------
        s.push_back({"size", OS_SETTLE, [](Context& ctx) { ctx.win.size = {900, 520}; }, [](Context& ctx) {
                         check_int(ctx, "size", "component.w", 900, ctx.win.size.first);
                         check_int(ctx, "size", "component.h", 520, ctx.win.size.second);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int w = 0;
                         int h = 0;
                         os_client_size(ctx.hwnd, w, h);
                         check_int(ctx, "size", "os_client.w", 900, w);
                         check_int(ctx, "size", "os_client.h", 520, h);
#endif
                     }});

        // 4. Size limits: SDL must clamp a request below the minimum. ----------
        s.push_back({"size_limits_min", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.win.size_limits.min_width  = 400;
                         ctx.win.size_limits.min_height = 300;
                         ctx.win.size                   = {200, 150};
                     },
                     [](Context& ctx) {
                         check_int(ctx, "size_limits_min", "component.w", 400, ctx.win.size.first);
                         check_int(ctx, "size_limits_min", "component.h", 300, ctx.win.size.second);
                     }});

        // 5. Size limits: and above the maximum. ------------------------------
        s.push_back({"size_limits_max", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.win.size_limits.max_width  = 1000;
                         ctx.win.size_limits.max_height = 700;
                         ctx.win.size                   = {2000, 1500};
                     },
                     [](Context& ctx) {
                         check_int(ctx, "size_limits_max", "component.w", 1000, ctx.win.size.first);
                         check_int(ctx, "size_limits_max", "component.h", 700, ctx.win.size.second);
                     }});

        // 6. Resizable. --------------------------------------------------------
        s.push_back({"resizable_off", OS_SETTLE, [](Context& ctx) { ctx.win.resizable = false; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "resizable_off", "os_thickframe", false, os_thickframe(ctx.hwnd));
#endif
                     }});
        s.push_back({"resizable_on", OS_SETTLE, [](Context& ctx) { ctx.win.resizable = true; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "resizable_on", "os_thickframe", true, os_thickframe(ctx.hwnd));
#endif
                     }});

        // 7. Decorations. ------------------------------------------------------
        s.push_back({"decorations_off", OS_SETTLE, [](Context& ctx) { ctx.win.decorations = false; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "decorations_off", "os_framed", false, os_framed(ctx.hwnd));
#endif
                     }});
        s.push_back({"decorations_on", OS_SETTLE, [](Context& ctx) { ctx.win.decorations = true; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "decorations_on", "os_framed", true, os_framed(ctx.hwnd));
#endif
                     }});

        // 8. Visibility. -------------------------------------------------------
        s.push_back({"hide", OS_SETTLE, [](Context& ctx) { ctx.win.visible = false; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "hide", "os_visible", false, os_visible(ctx.hwnd));
#endif
                     }});
        s.push_back({"show", OS_SETTLE, [](Context& ctx) { ctx.win.visible = true; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "show", "os_visible", true, os_visible(ctx.hwnd));
#endif
                     }});

        // 9. Opacity. ----------------------------------------------------------
        s.push_back({"opacity_half", OS_SETTLE, [](Context& ctx) { ctx.win.opacity = 0.5f; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         const int alpha = os_alpha(ctx.hwnd);
                         check_bool(ctx, "opacity_half", "os_alpha_near_127", true, alpha >= 125 && alpha <= 129);
#endif
                     }});
        s.push_back({"opacity_full", OS_SETTLE, [](Context& ctx) { ctx.win.opacity = 1.0f; }, [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_int(ctx, "opacity_full", "os_alpha_unlayered", -1, os_alpha(ctx.hwnd));
#endif
                     }});

        // 10. Maximize / restore. ---------------------------------------------
        s.push_back({"maximize", OS_SETTLE, [](Context& ctx) { ctx.win.maximized = true; }, [](Context& ctx) {
                         check_bool(ctx, "maximize", "component", true, ctx.win.maximized);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "maximize", "os_zoomed", true, os_zoomed(ctx.hwnd));
#endif
                     }});
        s.push_back({"unmaximize", OS_SETTLE, [](Context& ctx) { ctx.win.maximized = false; }, [](Context& ctx) {
                         check_bool(ctx, "unmaximize", "component", false, ctx.win.maximized);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "unmaximize", "os_zoomed", false, os_zoomed(ctx.hwnd));
#endif
                     }});

        // 11. Minimize / restore. ---------------------------------------------
        s.push_back({"iconify", OS_SETTLE, [](Context& ctx) { ctx.win.iconified = true; }, [](Context& ctx) {
                         check_bool(ctx, "iconify", "component", true, ctx.win.iconified);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "iconify", "os_iconic", true, os_iconic(ctx.hwnd));
#endif
                     }});
        s.push_back({"deiconify", OS_SETTLE, [](Context& ctx) { ctx.win.iconified = false; }, [](Context& ctx) {
                         check_bool(ctx, "deiconify", "component", false, ctx.win.iconified);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "deiconify", "os_iconic", false, os_iconic(ctx.hwnd));
#endif
                     }});

        // 12. Window level. ----------------------------------------------------
        s.push_back({"always_on_top", OS_SETTLE,
                     [](Context& ctx) { ctx.win.window_level = epix::window::WindowLevel::AlwaysOnTop; },
                     [](Context& ctx) {
                         check_int(ctx, "always_on_top", "component", 2, static_cast<int>(ctx.win.window_level));
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "always_on_top", "os_topmost", true, os_topmost(ctx.hwnd));
#endif
                     }});
        s.push_back({"level_normal", OS_SETTLE,
                     [](Context& ctx) { ctx.win.window_level = epix::window::WindowLevel::Normal; },
                     [](Context& ctx) {
                         check_int(ctx, "level_normal", "component", 1, static_cast<int>(ctx.win.window_level));
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "level_normal", "os_topmost", false, os_topmost(ctx.hwnd));
#endif
                     }});

        // 13. Position. --------------------------------------------------------
        s.push_back({"pos_topleft", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.win.pos_type = epix::window::PosType::TopLeft;
                         ctx.win.pos      = {150, 120};
                     },
                     [](Context& ctx) {
                         check_str(ctx, "pos_topleft", "final_pos", "(150,120)", pos_str(ctx.win));
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int x = 0;
                         int y = 0;
                         os_client_origin(ctx.hwnd, x, y);
                         check_str(ctx, "pos_topleft", "os_client_origin", "(150,120)", pair_str(x, y));
#endif
                     }});
        s.push_back({"pos_written_final", OS_SETTLE, [](Context& ctx) { ctx.win.final_pos = {250, 210}; },
                     [](Context& ctx) {
                         check_str(ctx, "pos_written_final", "final_pos", "(250,210)", pos_str(ctx.win));
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int x = 0;
                         int y = 0;
                         os_client_origin(ctx.hwnd, x, y);
                         check_str(ctx, "pos_written_final", "os_client_origin", "(250,210)", pair_str(x, y));
#endif
                     }});

        // 14. Window modes. ----------------------------------------------------
        s.push_back({"fullscreen", OS_SETTLE * 2,
                     [](Context& ctx) { ctx.win.window_mode = epix::window::WindowMode::Fullscreen; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int mw = 0;
                         int mh = 0;
                         os_monitor_size(ctx.hwnd, mw, mh);
                         int w = 0;
                         int h = 0;
                         os_client_size(ctx.hwnd, w, h);
                         check_int(ctx, "fullscreen", "os_client.w", mw, w);
                         check_int(ctx, "fullscreen", "os_client.h", mh, h);
#endif
                     }});
        s.push_back({"windowed_restore", OS_SETTLE * 2,
                     [](Context& ctx) { ctx.win.window_mode = epix::window::WindowMode::Windowed; },
                     [](Context& ctx) {
                         check_int(ctx, "windowed_restore", "size.w", 1000, ctx.win.size.first);
                         check_int(ctx, "windowed_restore", "size.h", 700, ctx.win.size.second);
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "windowed_restore", "os_framed", true, os_framed(ctx.hwnd));
#endif
                     }});
        s.push_back({"borderless_fullscreen", OS_SETTLE * 2,
                     [](Context& ctx) { ctx.win.window_mode = epix::window::WindowMode::BorderlessFullscreen; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int mw = 0;
                         int mh = 0;
                         os_monitor_size(ctx.hwnd, mw, mh);
                         int w = 0;
                         int h = 0;
                         os_client_size(ctx.hwnd, w, h);
                         check_int(ctx, "borderless_fullscreen", "os_client.w", mw, w);
                         check_int(ctx, "borderless_fullscreen", "os_client.h", mh, h);
                         check_bool(ctx, "borderless_fullscreen", "os_framed", false, os_framed(ctx.hwnd));
#endif
                     }});
        s.push_back({"windowed_again", OS_SETTLE * 2,
                     [](Context& ctx) { ctx.win.window_mode = epix::window::WindowMode::Windowed; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "windowed_again", "os_framed", true, os_framed(ctx.hwnd));
#endif
                     }});

        // 15. Cursor icon: standard shapes. -----------------------------------
        // Each shape is sampled only once the pointer has settled inside the
        // window, so the comparison is between two settled observations rather
        // than against a baseline taken while the pointer was still moving.
        s.push_back({"cursor_icon_ibeam", OS_SETTLE * 2,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_force_foreground(ctx.hwnd);
                         os_move_cursor_into(ctx.hwnd, 120, 120);
#endif
                         ctx.win.cursor_icon = epix::window::StandardCursor::IBeam;
                     },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         ctx.probe.cursor_before = os_cursor_handle();
                         check_bool(ctx, "cursor_icon_ibeam", "os_cursor_present", true,
                                    ctx.probe.cursor_before != nullptr);
#endif
                     }});
        s.push_back({"cursor_icon_arrow", OS_SETTLE * 2,
                     [](Context& ctx) { ctx.win.cursor_icon = epix::window::StandardCursor::Arrow; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "cursor_icon_arrow", "os_cursor_present", true,
                                    os_cursor_handle() != nullptr);
                         check_bool(ctx, "cursor_icon_arrow", "differs_from_ibeam", true,
                                    os_cursor_handle() != ctx.probe.cursor_before);
#endif
                     }});

        // 16. Custom cursor built from an RGBA image asset. -------------------
        s.push_back({"custom_cursor", OS_SETTLE,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         ctx.probe.icon_before = os_window_icon(ctx.hwnd);
#endif
                         ctx.win.cursor_icon = epix::window::CustomCursor{ctx.probe.pattern.value(), 0, 0};
                     },
                     [](Context& ctx) {
                         check_bool(ctx, "custom_cursor", "component_accepted", true,
                                    ctx.win.cursor_icon.index() == 1);
                     }});

        // 17. Window icon built from the same RGBA image asset. ---------------
        s.push_back({"window_icon", OS_SETTLE, [](Context& ctx) { ctx.win.icon = ctx.probe.pattern.value(); },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "window_icon", "os_icon_changed", true,
                                    os_window_icon(ctx.hwnd) != ctx.probe.icon_before);
#endif
                     }});

        // 18. Cursor modes. ----------------------------------------------------
        s.push_back({"cursor_hidden", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.win.cursor_mode = epix::window::CursorMode::Hidden;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 120, 120);
#endif
                     },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "cursor_hidden", "os_cursor_shown", false, os_cursor_shown());
                         check_bool(ctx, "cursor_hidden", "os_not_confined", false, os_cursor_confined_to(ctx.hwnd));
#endif
                     }});
        s.push_back({"cursor_normal", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.win.cursor_mode = epix::window::CursorMode::Normal;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 130, 130);
#endif
                     },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "cursor_normal", "os_cursor_shown", true, os_cursor_shown());
                         check_bool(ctx, "cursor_normal", "os_not_confined", false, os_cursor_confined_to(ctx.hwnd));
#endif
                     }});
        s.push_back({"cursor_captured", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.win.cursor_mode = epix::window::CursorMode::Captured;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 140, 140);
#endif
                     },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "cursor_captured", "os_cursor_shown", false, os_cursor_shown());
                         check_bool(ctx, "cursor_captured", "os_confined_to_window", true,
                                    os_cursor_confined_to(ctx.hwnd));
#endif
                     }});
        s.push_back({"cursor_disabled", OS_SETTLE,
                     [](Context& ctx) { ctx.win.cursor_mode = epix::window::CursorMode::Disabled; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "cursor_disabled", "os_cursor_shown", false, os_cursor_shown());
                         check_bool(ctx, "cursor_disabled", "os_confined_to_window", true,
                                    os_cursor_confined_to(ctx.hwnd));
#endif
                     }});
        s.push_back({"cursor_mode_restore", OS_SETTLE,
                     [](Context& ctx) { ctx.win.cursor_mode = epix::window::CursorMode::Normal; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         check_bool(ctx, "cursor_mode_restore", "os_not_confined", false,
                                    os_cursor_confined_to(ctx.hwnd));
#endif
                     }});

        // 19. Cursor position: a written position must warp the real pointer. -
        s.push_back({"cursor_warp", OS_SETTLE, [](Context& ctx) { ctx.win.cursor_pos = {123.0, 91.0}; },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int origin_x = 0;
                         int origin_y = 0;
                         os_client_origin(ctx.hwnd, origin_x, origin_y);
                         int cursor_x = 0;
                         int cursor_y = 0;
                         os_cursor_screen_pos(cursor_x, cursor_y);
                         check_str(ctx, "cursor_warp", "os_pointer", pair_str(origin_x + 123, origin_y + 91),
                                   pair_str(cursor_x, cursor_y));
#endif
                     }});

        // 20. Clipboard: writing the resource must reach the OS clipboard, and
        // an external change must come back into the resource. Both directions
        // are checked against Win32, not against the engine's own view.
        s.push_back({"clipboard_write", OS_SETTLE * 2,
                     [](Context& ctx) { ctx.clipboard.text = "epix-sdl3-clipboard-probe"; },
                     [](Context& ctx) {
                         check_str(ctx, "clipboard_write", "os_clipboard", "epix-sdl3-clipboard-probe",
                                   os_get_clipboard_text());
                         check_str(ctx, "clipboard_write", "resource_unchanged", "epix-sdl3-clipboard-probe",
                                   ctx.clipboard.text);
                     }});
        s.push_back({"clipboard_external_change", OS_SETTLE * 2,
                     [](Context& ctx) {
                         os_set_clipboard_text("epix-external-clipboard-value");
                         ctx.probe.cursor_before = nullptr;
                     },
                     [](Context& ctx) {
                         check_str(ctx, "clipboard_external_change", "resource_followed_os",
                                   "epix-external-clipboard-value", ctx.clipboard.text);
                     }});

        // 21. Cursor reported inside the window once the pointer is over it. --
        s.push_back({"cursor_in_window", OS_SETTLE,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 200, 200);
#endif
                     },
                     [](Context& ctx) {
                         check_bool(ctx, "cursor_in_window", "component", true, ctx.win.cursor_in_window);
                     }});

        // 22. `Window::cursor_pos` must follow the real pointer. --------------
        // `documentation/window/window.md` documents a client-area position that
        // may fall outside [0, size) once the cursor leaves the window, so the
        // readback is checked inside the window and then, with a button held,
        // outside it - the engine passes the position through unclamped.
        s.push_back({"cursor_readback_inside", OS_SETTLE,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 350, 250);
#endif
                     },
                     [](Context& ctx) {
                         const int x = static_cast<int>(ctx.win.cursor_pos.first);
                         const int y = static_cast<int>(ctx.win.cursor_pos.second);
                         check_bool(ctx, "cursor_readback_inside", "follows_pointer", true,
                                    std::abs(x - 350) <= 2 && std::abs(y - 250) <= 2);
                     }});
        s.push_back({"cursor_readback_outside", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.probe.cursor_before = nullptr;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         // Hold the right button so SDL keeps delivering motion
                         // once the pointer leaves the window, then move well
                         // outside the client area.
                         os_send_mouse(MOUSEEVENTF_RIGHTDOWN);
                         int width  = 0;
                         int height = 0;
                         os_client_size(ctx.hwnd, width, height);
                         os_move_cursor_into(ctx.hwnd, width + 150, 200);
#endif
                     },
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         int width  = 0;
                         int height = 0;
                         os_client_size(ctx.hwnd, width, height);
                         const int x = static_cast<int>(ctx.win.cursor_pos.first);
                         check_bool(ctx, "cursor_readback_outside", "reported_unclamped", true, x > width);
#endif
                     }});
        s.push_back({"cursor_release", 2,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_send_mouse(MOUSEEVENTF_RIGHTUP);
                         os_move_cursor_into(ctx.hwnd, 200, 200);
#endif
                     },
                     [](Context&) {}});

        // 23. Every mouse button must arrive as the matching engine button. --
        // `MouseButtonRight` is `MouseButton2` and `MouseButtonMiddle` is
        // `MouseButton3`, while SDL numbers LEFT=1, MIDDLE=2, RIGHT=3, so the
        // three buttons have to be checked individually.
        s.push_back({"mouse_button_left", OS_SETTLE,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 400, 300);
                         os_send_mouse(MOUSEEVENTF_LEFTDOWN);
                         os_send_mouse(MOUSEEVENTF_LEFTUP);
#endif
                     },
                     [](Context& ctx) {
                         check_bool(ctx, "mouse_button_left", "engine_saw_MouseButtonLeft", true,
                                    (ctx.probe.buttons_pressed & (1u << 0)) != 0);
                     }});
        s.push_back({"mouse_button_right", OS_SETTLE,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 400, 300);
                         os_send_mouse(MOUSEEVENTF_RIGHTDOWN);
                         os_send_mouse(MOUSEEVENTF_RIGHTUP);
#endif
                     },
                     [](Context& ctx) {
                         check_bool(ctx, "mouse_button_right", "engine_saw_MouseButtonRight", true,
                                    (ctx.probe.buttons_pressed & (1u << 1)) != 0);
                         check_bool(ctx, "mouse_button_right", "not_reported_as_middle", false,
                                    (ctx.probe.buttons_pressed & (1u << 2)) != 0);
                     }});
        s.push_back({"mouse_button_middle", OS_SETTLE,
                     [](Context& ctx) {
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_move_cursor_into(ctx.hwnd, 400, 300);
                         os_send_mouse(MOUSEEVENTF_MIDDLEDOWN);
                         os_send_mouse(MOUSEEVENTF_MIDDLEUP);
#endif
                     },
                     [](Context& ctx) {
                         check_bool(ctx, "mouse_button_middle", "engine_saw_MouseButtonMiddle", true,
                                    (ctx.probe.buttons_pressed & (1u << 2)) != 0);
                     }});
        s.push_back({"mouse_wheel", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.probe.scroll_events = 0;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         // The wheel is attributed to the window under the
                         // pointer, so it has to be over this window.
                         os_move_cursor_into(ctx.hwnd, 400, 300);
                         os_send_mouse(MOUSEEVENTF_WHEEL, 120);
#endif
                     },
                     [](Context& ctx) {
                         check_bool(ctx, "mouse_wheel", "engine_saw_MouseScroll", true,
                                    ctx.probe.scroll_events > 0);
                     }});

        // 24. A drop of several files is ONE event carrying every path. -------
        s.push_back({"file_drop_multiple", OS_SETTLE,
                     [](Context& ctx) {
                         ctx.probe.drop_events   = 0;
                         ctx.probe.last_drop_paths = 0;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         os_post_file_drop(ctx.hwnd, {"C:/probe/first.png", "C:/probe/second.png"});
#endif
                     },
                     [](Context& ctx) {
                         check_int(ctx, "file_drop_multiple", "events", 1, ctx.probe.drop_events);
                         check_int(ctx, "file_drop_multiple", "paths_in_the_event", 2, ctx.probe.last_drop_paths);
                     }});

        return s;
    }();
    return table;
}

void run_automatic(Context& ctx) {
    auto& table = steps();
    if (ctx.probe.step >= static_cast<int>(table.size())) {
        if (!ctx.probe.automatic_done) {
            ctx.probe.automatic_done = true;
            ctx.probe.interactive    = true;
            emit("AUTO-COMPLETE passed=" + num(ctx.probe.passed) + " failed=" + num(ctx.probe.failed));
            emit("INTERACTIVE started - drive the window with real input; Escape closes it");
        }
        return;
    }
    Step& step = table[static_cast<std::size_t>(ctx.probe.step)];
    if (!ctx.probe.applied) {
        emit(std::string("STEP ") + step.name + " " + component_report(ctx.win));
        if (step.apply) step.apply(ctx);
        ctx.probe.applied       = true;
        ctx.probe.step_deadline = now_ms() + step.settle_ms;
        return;
    }
    if (now_ms() < ctx.probe.step_deadline) return;
    emit(std::string("OS ") + step.name + " " + os_report(ctx.hwnd));
    if (step.check) step.check(ctx);
    ctx.probe.applied = false;
    ++ctx.probe.step;
}

void handle_interactive_key(Context& ctx, epix::input::KeyCode key) {
    using epix::input::KeyCode;
    using namespace epix::window;
    Window& w = ctx.win;
    switch (key) {
        case KeyCode::KeyF1:
            w.resizable = !w.resizable;
            emit(std::string("ACTION resizable=") + yn(w.resizable));
            break;
        case KeyCode::KeyF2:
            w.decorations = !w.decorations;
            emit(std::string("ACTION decorations=") + yn(w.decorations));
            break;
        case KeyCode::KeyF3:
            w.visible = !w.visible;
            emit(std::string("ACTION visible=") + yn(w.visible));
            break;
        case KeyCode::KeyF4:
            w.maximized = !w.maximized;
            emit(std::string("ACTION maximized=") + yn(w.maximized));
            break;
        case KeyCode::KeyF5:
            w.iconified = !w.iconified;
            emit(std::string("ACTION iconified=") + yn(w.iconified));
            break;
        case KeyCode::KeyF6:
            w.window_level = w.window_level == WindowLevel::Normal ? WindowLevel::AlwaysOnTop : WindowLevel::Normal;
            emit(std::string("ACTION window_level=") + num(static_cast<int>(w.window_level)));
            break;
        case KeyCode::KeyF7:
            w.cursor_mode = w.cursor_mode == CursorMode::Normal     ? CursorMode::Hidden
                            : w.cursor_mode == CursorMode::Hidden   ? CursorMode::Captured
                            : w.cursor_mode == CursorMode::Captured ? CursorMode::Disabled
                                                                    : CursorMode::Normal;
            emit(std::string("ACTION cursor_mode=") + num(static_cast<int>(w.cursor_mode)));
            break;
        case KeyCode::KeyF8: {
            static int index                     = 0;
            static const StandardCursor shapes[] = {StandardCursor::Arrow,     StandardCursor::IBeam,
                                                    StandardCursor::Crosshair, StandardCursor::Hand,
                                                    StandardCursor::ResizeAll, StandardCursor::ResizeNS,
                                                    StandardCursor::ResizeEW,  StandardCursor::NotAllowed};
            index         = (index + 1) % 8;
            w.cursor_icon = shapes[index];
            emit(std::string("ACTION cursor_icon=") + num(index));
            break;
        }
        case KeyCode::KeyF9:
            w.request_attention();
            emit("ACTION request_attention");
            break;
        case KeyCode::KeyF10:
            w.opacity = w.opacity > 0.9f ? 0.5f : 1.0f;
            emit("ACTION opacity=" + std::to_string(w.opacity));
            break;
        case KeyCode::KeyF11:
            w.window_mode = w.window_mode == WindowMode::Windowed       ? WindowMode::Fullscreen
                            : w.window_mode == WindowMode::Fullscreen ? WindowMode::BorderlessFullscreen
                                                                      : WindowMode::Windowed;
            emit(std::string("ACTION window_mode=") + num(static_cast<int>(w.window_mode)));
            break;
        case KeyCode::KeyF12:
            w.title = "Epix SDL3 Probe " + num(++ctx.probe.titles);
            emit("ACTION title=" + w.title);
            break;
        case KeyCode::KeySpace:
            w.size = w.size.first == 640 ? std::pair<int, int>{900, 520} : std::pair<int, int>{640, 480};
            emit("ACTION size=" + size_str(w));
            break;
        case KeyCode::KeyEscape:
            emit("ACTION escape -> despawn window entity");
            ctx.commands.entity(ctx.window).despawn();
            break;
        default:
            break;
    }
}

}  // namespace

int main() {
    using namespace epix;
    using namespace epix::ecs;
    using namespace epix::app;

    window::Window primary;
    primary.title    = "Epix SDL3 Feature Probe";
    primary.size     = {640, 480};
    primary.pos      = {80, 80};
    primary.pos_type = window::PosType::TopLeft;

    App app = App::create();
    main_thread_id = current_thread_id();
    // The engine logs to stdout, which is block buffered when redirected; the
    // probe is watched live from outside, so every record has to land at once.
    spdlog::default_logger()->flush_on(spdlog::level::trace);

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{})
        .plugin_scope([&](window::WindowPlugin& plugin) {
            plugin.exit_condition = window::ExitCondition::OnPrimaryClosed;
            plugin.primary_window = primary;
        })
        .add_plugins(sdl3::SDL3Plugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(input::InputPlugin{})
        .add_systems(Update, into(window::log_events, input::log_inputs).set_name("event log"))
        .add_systems(Update,
                     into([](Commands commands,
                             Query<Item<Entity, Mut<Window>>, With<window::PrimaryWindow>> windows,
                             ResMut<window::Clipboard> clipboard,
                             ResMut<Assets<Image>> images,
                             EventReader<input::KeyInput> keys,
                             EventReader<input::MouseButtonInput> mouse_buttons,
                             EventReader<input::MouseScroll> scrolls,
                             EventReader<window::FileDrop> drops,
                             Local<Probe> state) {
                         const std::uint64_t now = now_ms();
                         if (state->start == 0) state->start = now;
                         ++state->frame;

                         // Latch what the engine reported, so a step can assert on
                         // events that already went past.
                         for (auto&& [button, pressed, window] : mouse_buttons.read()) {
                             const int index = static_cast<int>(button);
                             if (pressed && index >= 0 && index < 32) state->buttons_pressed |= 1u << index;
                         }
                         for (auto&& [xoffset, yoffset, window] : scrolls.read()) {
                             ++state->scroll_events;
                         }
                         for (auto&& [window, paths] : drops.read()) {
                             ++state->drop_events;
                             state->last_drop_paths = static_cast<int>(paths.size());
                         }
                         if (state->last_frame_ms != 0) {
                             state->max_gap_ms = std::max(state->max_gap_ms, now - state->last_frame_ms);
                         }
                         const std::uint64_t gap = state->last_frame_ms == 0 ? 0 : now - state->last_frame_ms;
                         state->last_frame_ms    = now;

                         Entity entity{};
                         Window* window = nullptr;
                         bool found     = false;
                         for (auto&& [id, description] : windows.iter()) {
                             entity = id;
                             window = &description.get_mut();
                             found  = true;
                             break;
                         }

                         if (now - state->last_hb >= 20) {
                             state->last_hb = now;
                             const std::string detail =
                                 window != nullptr ? ("size=" + size_str(*window) + " pos=" + pos_str(*window))
                                                   : std::string("no-window");
                             emit("HB frame=" + std::to_string(state->frame) + " t=" +
                                  std::to_string(now - state->start) + "ms gap=" + std::to_string(gap) +
                                  "ms maxgap=" + std::to_string(state->max_gap_ms) + "ms tid=" +
                                  std::to_string(current_thread_id()) + " " + detail);
                         }
                         // The executor may run a system on any worker thread.
                         // Native window access has to happen on the thread that
                         // owns the window, so defer to the next frame instead.
                         if (current_thread_id() != main_thread_id) return;

                         if (!found || window == nullptr) return;
#if defined(_WIN32) && !defined(EPIX_PROBE_SKIP_OS)
                         static HWND cached = nullptr;
                         if (cached == nullptr || !IsWindow(cached)) cached = find_own_window();
                         HWND hwnd = cached;
#else
                         HWND hwnd = nullptr;
#endif

                         if (!state->pattern.has_value()) {
                             // A 16x16 opaque checkerboard, enough to be a
                             // distinguishable cursor and window icon.
                             std::vector<std::uint8_t> pixels(16u * 16u * 4u, 0);
                             for (int y = 0; y < 16; ++y) {
                                 for (int x = 0; x < 16; ++x) {
                                     const std::size_t i = static_cast<std::size_t>((y * 16 + x) * 4);
                                     const bool on       = ((x / 4) + (y / 4)) % 2 == 0;
                                     pixels[i + 0]       = on ? 255 : 0;
                                     pixels[i + 1]       = 0;
                                     pixels[i + 2]       = on ? 0 : 255;
                                     pixels[i + 3]       = 255;
                                 }
                             }
                             auto created = Image::create2d(16, 16, wgpu::TextureFormat::eRGBA8Unorm, pixels);
                             if (created) state->pattern = images->add(std::move(*created));
                         }

                         Context ctx{commands, *state, entity, *window, hwnd, *images, *clipboard};
                         if (!state->interactive) {
                             run_automatic(ctx);
                         } else {
                             for (auto&& [key, scancode, pressed, repeat, source] : keys.read()) {
                                 (void)scancode;
                                 (void)source;
                                 if (pressed && !repeat) handle_interactive_key(ctx, key);
                             }
                         }
                     })
                         .set_name("probe"));
    app.run();
    return 0;
}
