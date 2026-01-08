export module epix.input;

export import :enums;
export import :events;
export import :button;

import epix.core;

using namespace core;

export namespace input {
struct InputPlugin {
    void build(App& app);
};
void log_inputs(EventReader<KeyInput> key_reader,
                EventReader<MouseButtonInput> mouse_reader,
                EventReader<MouseMove> mouse_move_reader,
                EventReader<MouseScroll> mouse_scroll_reader);
}  // namespace input
