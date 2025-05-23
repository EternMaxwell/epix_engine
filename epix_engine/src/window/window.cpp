#include "epix/window.h"

EPIX_API bool epix::window::window::Cursor::operator==(
    const epix::window::window::Cursor& other
) const {
    return icon == other.icon && mode == other.mode;
}

EPIX_API bool epix::window::window::WindowPosition::operator==(
    const epix::window::window::WindowPosition& other
) const {
    return x == other.x && y == other.y;
}

EPIX_API bool epix::window::window::WindowSizeLimit::operator==(
    const epix::window::window::WindowSizeLimit& other
) const {
    return min_width == other.min_width && min_height == other.min_height &&
           max_width == other.max_width && max_height == other.max_height;
}

EPIX_API std::pair<int, int> epix::window::window::Window::get_position(
) const {
    if (position) {
        return {position->x, position->y};
    }
    return {0, 0};
}
EPIX_API void epix::window::window::Window::set_position(int x, int y) {
    if (!position) {
        position = WindowPosition{x, y};
    } else {
        position->x = x;
        position->y = y;
    }
}
EPIX_API const epix::window::window::WindowFrameSize&
epix::window::window::Window::get_frame_size() const {
    return frame_size;
}
EPIX_API void epix::window::window::Window::set_maximized(bool maximized) {
    internal.maximize_request = maximized;
}
EPIX_API int epix::window::window::Window::width() const {
    return window_size.width;
}
EPIX_API int epix::window::window::Window::height() const {
    return window_size.height;
}
EPIX_API std::pair<int, int> epix::window::window::Window::size() const {
    return {window_size.width, window_size.height};
}
EPIX_API std::pair<int, int> epix::window::window::Window::physical_size(
) const {
    return {window_size.physical_width, window_size.physical_height};
}
EPIX_API void epix::window::window::Window::set_size(int width, int height) {
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
EPIX_API std::tuple<bool, double, double>
epix::window::window::Window::cursor_position() const {
    return internal.cursor_position;
}
/**
 * @brief Get cursor position normalized (scaled to (0, 1)).
 */
EPIX_API std::pair<double, double>
epix::window::window::Window::cursor_position_normalized() const {
    auto [_, posx, posy] = cursor_position();
    auto [width, height] = size();
    return {posx / width, posy / height};
}
EPIX_API void epix::window::window::Window::set_cursor_position(
    double x, double y
) {
    auto& [_, posx, posy] = internal.cursor_position;
    posx                  = x;
    posy                  = y;
}