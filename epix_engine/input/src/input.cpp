#include <spdlog/spdlog.h>

#include "epix/input.hpp"

namespace epix {
void input::log_inputs(EventReader<KeyInput> key_reader,
                       EventReader<MouseButtonInput> mouse_reader,
                       EventReader<MouseMove> mouse_move_reader,
                       EventReader<MouseScroll> mouse_scroll_reader) {
    for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
        spdlog::info("Key: {}, Scancode: {}, Pressed: {}, Repeat: {}", static_cast<int>(key), scancode, pressed,
                     repeat);
    }
    for (auto&& [button, pressed, window] : mouse_reader.read()) {
        spdlog::info("Mouse Button: {}, Pressed: {}", static_cast<int>(button), pressed);
    }
    for (auto&& [delta] : mouse_move_reader.read()) {
        spdlog::info("Mouse Move: {}, {}", delta.first, delta.second);
    }
    for (auto&& [xoffset, yoffset, window] : mouse_scroll_reader.read()) {
        spdlog::info("Mouse Scroll: {}, {}", xoffset, yoffset);
    }
}
}  // namespace epix