// epix_input - Pure C++20 Module (No Headers)
// Input handling module for keyboard, mouse, and game controllers

module;

// Global module fragment - standard library headers only
#include <algorithm>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// Third-party dependencies
#include <spdlog/spdlog.h>

export module epix_input;

// Import module dependencies
import epix_core;

// ============================================================================
// Forward declarations
// ============================================================================

export namespace epix::input {
    enum class KeyCode : int;
    enum class MouseButton : int;
    
    template <typename T>
    struct ButtonInput;
    
    struct InputPlugin;
    
    namespace events {
        struct KeyInput;
        struct MouseButtonInput;
        struct MouseMove;
        struct MouseScroll;
    }
}

// ============================================================================
// Enums
// ============================================================================

export namespace epix::input {

enum class KeyCode : int {
    KeyA,
    KeyB,
    KeyC,
    KeyD,
    KeyE,
    KeyF,
    KeyG,
    KeyH,
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyM,
    KeyN,
    KeyO,
    KeyP,
    KeyQ,
    KeyR,
    KeyS,
    KeyT,
    KeyU,
    KeyV,
    KeyW,
    KeyX,
    KeyY,
    KeyZ,
    Key0,
    Key1,
    Key2,
    Key3,
    Key4,
    Key5,
    Key6,
    Key7,
    Key8,
    Key9,
    KeySpace,
    KeyApostrophe,
    KeyComma,
    KeyMinus,
    KeyPeriod,
    KeySlash,
    KeySemicolon,
    KeyEqual,
    KeyLeftBracket,
    KeyBackslash,
    KeyRightBracket,
    KeyGraveAccent,
    KeyWorld1,
    KeyWorld2,
    KeyEscape,
    KeyEnter,
    KeyTab,
    KeyBackspace,
    KeyInsert,
    KeyDelete,
    KeyRight,
    KeyLeft,
    KeyDown,
    KeyUp,
    KeyPageUp,
    KeyPageDown,
    KeyHome,
    KeyEnd,
    KeyCapsLock,
    KeyScrollLock,
    KeyNumLock,
    KeyPrintScreen,
    KeyPause,
    KeyF1,
    KeyF2,
    KeyF3,
    KeyF4,
    KeyF5,
    KeyF6,
    KeyF7,
    KeyF8,
    KeyF9,
    KeyF10,
    KeyF11,
    KeyF12,
    KeyF13,
    KeyF14,
    KeyF15,
    KeyF16,
    KeyF17,
    KeyF18,
    KeyF19,
    KeyF20,
    KeyF21,
    KeyF22,
    KeyF23,
    KeyF24,
    KeyF25,
    KeyKp0,
    KeyKp1,
    KeyKp2,
    KeyKp3,
    KeyKp4,
    KeyKp5,
    KeyKp6,
    KeyKp7,
    KeyKp8,
    KeyKp9,
    KeyKpDecimal,
    KeyKpDivide,
    KeyKpMultiply,
    KeyKpSubtract,
    KeyKpAdd,
    KeyKpEnter,
    KeyKpEqual,
    KeyLeftShift,
    KeyLeftControl,
    KeyLeftAlt,
    KeyLeftSuper,
    KeyRightShift,
    KeyRightControl,
    KeyRightAlt,
    KeyRightSuper,
    KeyMenu,
    KeyLast = KeyMenu,

    KeyUnknown = -1,
};

// Function declarations
std::string_view key_name(KeyCode key);

enum class MouseButton : int {
    MouseButton1,
    MouseButton2,
    MouseButton3,
    MouseButton4,
    MouseButton5,
    MouseButton6,
    MouseButton7,
    MouseButton8,
    MouseButtonLast,
    MouseButtonLeft   = MouseButton1,
    MouseButtonRight  = MouseButton2,
    MouseButtonMiddle = MouseButton3,

    MouseButtonUnknown = -1,
};

std::string_view mouse_button_name(MouseButton button);

}  // namespace epix::input

// ============================================================================
// Events
// ============================================================================

export namespace epix::input::events {
    
using namespace epix::core;

struct KeyInput {
    KeyCode key;
    int scancode;
    bool pressed;
    bool repeat;
    Entity window;
};

struct MouseButtonInput {
    MouseButton button;
    bool pressed;
    Entity window;
};

struct MouseMove {
    std::pair<double, double> delta;
};

struct MouseScroll {
    double xoffset;
    double yoffset;
    Entity window;
};

}  // namespace epix::input::events

// ============================================================================
// ButtonInput template
// ============================================================================

export namespace epix::input {

using namespace epix::core;
using namespace events;

template <typename T>
struct ButtonInput {};

// Specialization for KeyCode
template <>
struct ButtonInput<KeyCode> {
public:
    ButtonInput() = default;

    bool just_pressed(KeyCode key) const { return m_just_pressed.contains(key); }
    bool just_released(KeyCode key) const { return m_just_released.contains(key); }
    bool pressed(KeyCode key) const { return m_pressed.contains(key); }

    auto just_pressed_keys() const { return std::views::all(m_just_pressed); }
    auto just_released_keys() const { return std::views::all(m_just_released); }
    auto pressed_keys() const { return std::views::all(m_pressed); }

    bool any_just_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return just_pressed(key); });
    }
    bool any_just_released(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return just_released(key); });
    }
    bool any_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return pressed(key); });
    }

    bool all_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::all_of(keys, [&](KeyCode key) { return pressed(key); });
    }

    static void collect_events(ResMut<ButtonInput<KeyCode>> input, 
                               EventReader<KeyInput> reader);

private:
    std::unordered_set<KeyCode> m_just_pressed;
    std::unordered_set<KeyCode> m_just_released;
    std::unordered_set<KeyCode> m_pressed;
};

// Specialization for MouseButton
template <>
struct ButtonInput<MouseButton> {
public:
    ButtonInput() = default;

    bool just_pressed(MouseButton button) const { return m_just_pressed.contains(button); }
    bool just_released(MouseButton button) const { return m_just_released.contains(button); }
    bool pressed(MouseButton button) const { return m_pressed.contains(button); }

    auto just_pressed_buttons() const { return std::views::all(m_just_pressed); }
    auto just_released_buttons() const { return std::views::all(m_just_released); }
    auto pressed_buttons() const { return std::views::all(m_pressed); }

    bool any_just_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return just_pressed(button); });
    }
    bool any_just_released(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return just_released(button); });
    }
    bool any_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return pressed(button); });
    }

    bool all_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::all_of(buttons, [&](MouseButton button) { return pressed(button); });
    }

    static void collect_events(ResMut<ButtonInput<MouseButton>> input, 
                               EventReader<MouseButtonInput> reader);

private:
    std::unordered_set<MouseButton> m_just_pressed;
    std::unordered_set<MouseButton> m_just_released;
    std::unordered_set<MouseButton> m_pressed;
};

}  // namespace epix::input

// ============================================================================
// InputPlugin
// ============================================================================

export namespace epix::input {

using namespace epix::core;

struct InputPlugin {
    void build(App& app);
};

// Utility functions
void log_inputs(EventReader<KeyInput> key_reader,
                EventReader<MouseButtonInput> mouse_reader,
                EventReader<MouseMove> mouse_move_reader,
                EventReader<MouseScroll> mouse_scroll_reader);

}  // namespace epix::input

// ============================================================================
// Prelude namespace for convenient imports
// ============================================================================

export namespace epix::input::prelude {
    using input::ButtonInput;
    using input::InputPlugin;
    using input::KeyCode;
    using input::MouseButton;
}

// Re-export in epix namespace
export namespace epix {
    using namespace input::prelude;
}

// ============================================================================
// std::hash specializations (must be in std namespace)
// ============================================================================

export namespace std {
    template <>
    struct hash<epix::input::KeyCode> {
        size_t operator()(const epix::input::KeyCode& key) const { 
            return std::hash<int>()(static_cast<int>(key)); 
        }
    };

    template <>
    struct hash<epix::input::MouseButton> {
        size_t operator()(const epix::input::MouseButton& button) const {
            return std::hash<int>()(static_cast<int>(button));
        }
    };
}
