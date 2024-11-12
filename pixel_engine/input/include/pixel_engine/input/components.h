#pragma once

#include <pixel_engine/app.h>
#include <pixel_engine/window.h>

namespace pixel_engine {
namespace input {
namespace components {
using namespace prelude;
using namespace window::components;

template <typename T>
struct ButtonInput {};

enum KeyCode {
    KeyA            = GLFW_KEY_A,
    KeyB            = GLFW_KEY_B,
    KeyC            = GLFW_KEY_C,
    KeyD            = GLFW_KEY_D,
    KeyE            = GLFW_KEY_E,
    KeyF            = GLFW_KEY_F,
    KeyG            = GLFW_KEY_G,
    KeyH            = GLFW_KEY_H,
    KeyI            = GLFW_KEY_I,
    KeyJ            = GLFW_KEY_J,
    KeyK            = GLFW_KEY_K,
    KeyL            = GLFW_KEY_L,
    KeyM            = GLFW_KEY_M,
    KeyN            = GLFW_KEY_N,
    KeyO            = GLFW_KEY_O,
    KeyP            = GLFW_KEY_P,
    KeyQ            = GLFW_KEY_Q,
    KeyR            = GLFW_KEY_R,
    KeyS            = GLFW_KEY_S,
    KeyT            = GLFW_KEY_T,
    KeyU            = GLFW_KEY_U,
    KeyV            = GLFW_KEY_V,
    KeyW            = GLFW_KEY_W,
    KeyX            = GLFW_KEY_X,
    KeyY            = GLFW_KEY_Y,
    KeyZ            = GLFW_KEY_Z,
    Key0            = GLFW_KEY_0,
    Key1            = GLFW_KEY_1,
    Key2            = GLFW_KEY_2,
    Key3            = GLFW_KEY_3,
    Key4            = GLFW_KEY_4,
    Key5            = GLFW_KEY_5,
    Key6            = GLFW_KEY_6,
    Key7            = GLFW_KEY_7,
    Key8            = GLFW_KEY_8,
    Key9            = GLFW_KEY_9,
    KeySpace        = GLFW_KEY_SPACE,
    KeyApostrophe   = GLFW_KEY_APOSTROPHE,
    KeyComma        = GLFW_KEY_COMMA,
    KeyMinus        = GLFW_KEY_MINUS,
    KeyPeriod       = GLFW_KEY_PERIOD,
    KeySlash        = GLFW_KEY_SLASH,
    KeySemicolon    = GLFW_KEY_SEMICOLON,
    KeyEqual        = GLFW_KEY_EQUAL,
    KeyLeftBracket  = GLFW_KEY_LEFT_BRACKET,
    KeyBackslash    = GLFW_KEY_BACKSLASH,
    KeyRightBracket = GLFW_KEY_RIGHT_BRACKET,
    KeyGraveAccent  = GLFW_KEY_GRAVE_ACCENT,
    KeyWorld1       = GLFW_KEY_WORLD_1,
    KeyWorld2       = GLFW_KEY_WORLD_2,
    KeyEscape       = GLFW_KEY_ESCAPE,
    KeyEnter        = GLFW_KEY_ENTER,
    KeyTab          = GLFW_KEY_TAB,
    KeyBackspace    = GLFW_KEY_BACKSPACE,
    KeyInsert       = GLFW_KEY_INSERT,
    KeyDelete       = GLFW_KEY_DELETE,
    KeyRight        = GLFW_KEY_RIGHT,
    KeyLeft         = GLFW_KEY_LEFT,
    KeyDown         = GLFW_KEY_DOWN,
    KeyUp           = GLFW_KEY_UP,
    KeyPageUp       = GLFW_KEY_PAGE_UP,
    KeyPageDown     = GLFW_KEY_PAGE_DOWN,
    KeyHome         = GLFW_KEY_HOME,
    KeyEnd          = GLFW_KEY_END,
    KeyCapsLock     = GLFW_KEY_CAPS_LOCK,
    KeyScrollLock   = GLFW_KEY_SCROLL_LOCK,
    KeyNumLock      = GLFW_KEY_NUM_LOCK,
    KeyPrintScreen  = GLFW_KEY_PRINT_SCREEN,
    KeyPause        = GLFW_KEY_PAUSE,
    KeyF1           = GLFW_KEY_F1,
    KeyF2           = GLFW_KEY_F2,
    KeyF3           = GLFW_KEY_F3,
    KeyF4           = GLFW_KEY_F4,
    KeyF5           = GLFW_KEY_F5,
    KeyF6           = GLFW_KEY_F6,
    KeyF7           = GLFW_KEY_F7,
    KeyF8           = GLFW_KEY_F8,
    KeyF9           = GLFW_KEY_F9,
    KeyF10          = GLFW_KEY_F10,
    KeyF11          = GLFW_KEY_F11,
    KeyF12          = GLFW_KEY_F12,
    KeyF13          = GLFW_KEY_F13,
    KeyF14          = GLFW_KEY_F14,
    KeyF15          = GLFW_KEY_F15,
    KeyF16          = GLFW_KEY_F16,
    KeyF17          = GLFW_KEY_F17,
    KeyF18          = GLFW_KEY_F18,
    KeyF19          = GLFW_KEY_F19,
    KeyF20          = GLFW_KEY_F20,
    KeyF21          = GLFW_KEY_F21,
    KeyF22          = GLFW_KEY_F22,
    KeyF23          = GLFW_KEY_F23,
    KeyF24          = GLFW_KEY_F24,
    KeyF25          = GLFW_KEY_F25,
    KeyKp0          = GLFW_KEY_KP_0,
    KeyKp1          = GLFW_KEY_KP_1,
    KeyKp2          = GLFW_KEY_KP_2,
    KeyKp3          = GLFW_KEY_KP_3,
    KeyKp4          = GLFW_KEY_KP_4,
    KeyKp5          = GLFW_KEY_KP_5,
    KeyKp6          = GLFW_KEY_KP_6,
    KeyKp7          = GLFW_KEY_KP_7,
    KeyKp8          = GLFW_KEY_KP_8,
    KeyKp9          = GLFW_KEY_KP_9,
    KeyKpDecimal    = GLFW_KEY_KP_DECIMAL,
    KeyKpDivide     = GLFW_KEY_KP_DIVIDE,
    KeyKpMultiply   = GLFW_KEY_KP_MULTIPLY,
    KeyKpSubtract   = GLFW_KEY_KP_SUBTRACT,
    KeyKpAdd        = GLFW_KEY_KP_ADD,
    KeyKpEnter      = GLFW_KEY_KP_ENTER,
    KeyKpEqual      = GLFW_KEY_KP_EQUAL,
    KeyLeftShift    = GLFW_KEY_LEFT_SHIFT,
    KeyLeftControl  = GLFW_KEY_LEFT_CONTROL,
    KeyLeftAlt      = GLFW_KEY_LEFT_ALT,
    KeyLeftSuper    = GLFW_KEY_LEFT_SUPER,
    KeyRightShift   = GLFW_KEY_RIGHT_SHIFT,
    KeyRightControl = GLFW_KEY_RIGHT_CONTROL,
    KeyRightAlt     = GLFW_KEY_RIGHT_ALT,
    KeyRightSuper   = GLFW_KEY_RIGHT_SUPER,
    KeyMenu         = GLFW_KEY_MENU,
    KeyLast         = GLFW_KEY_LAST,
};  // namespace resources

struct KeyHash {
    size_t operator()(const KeyCode& key) const;
};

template <>
struct ButtonInput<KeyCode> {
   public:
    ButtonInput<KeyCode>(Entity window);

    bool just_pressed(KeyCode key) const;
    bool just_released(KeyCode key) const;
    bool pressed(KeyCode key) const;

    const spp::sparse_hash_set<KeyCode, KeyHash>& just_pressed_keys() const;
    const spp::sparse_hash_set<KeyCode, KeyHash>& just_released_keys() const;
    const spp::sparse_hash_set<KeyCode, KeyHash>& pressed_keys() const;

    bool any_just_pressed(const std::vector<KeyCode>& keys) const;
    bool any_just_released(const std::vector<KeyCode>& keys) const;
    bool any_pressed(const std::vector<KeyCode>& keys) const;

    bool all_pressed(const std::vector<KeyCode>& keys) const;

    const char* key_name(KeyCode key) const;

    Entity linked_window() const;

   private:
    Entity m_window;

    spp::sparse_hash_set<KeyCode, KeyHash> m_just_pressed;
    spp::sparse_hash_set<KeyCode, KeyHash> m_just_released;
    spp::sparse_hash_set<KeyCode, KeyHash> m_pressed;

    friend void update_key_button_input(
        ButtonInput<KeyCode>& key_input, GLFWwindow* window
    );
};

void update_key_button_input(
    ButtonInput<KeyCode>& key_input, GLFWwindow* window
);

enum MouseButton {
    MouseButton1      = GLFW_MOUSE_BUTTON_1,
    MouseButton2      = GLFW_MOUSE_BUTTON_2,
    MouseButton3      = GLFW_MOUSE_BUTTON_3,
    MouseButton4      = GLFW_MOUSE_BUTTON_4,
    MouseButton5      = GLFW_MOUSE_BUTTON_5,
    MouseButton6      = GLFW_MOUSE_BUTTON_6,
    MouseButton7      = GLFW_MOUSE_BUTTON_7,
    MouseButton8      = GLFW_MOUSE_BUTTON_8,
    MouseButtonLast   = GLFW_MOUSE_BUTTON_LAST,
    MouseButtonLeft   = GLFW_MOUSE_BUTTON_LEFT,
    MouseButtonRight  = GLFW_MOUSE_BUTTON_RIGHT,
    MouseButtonMiddle = GLFW_MOUSE_BUTTON_MIDDLE
};

struct MouseButtonHash {
    size_t operator()(const MouseButton& button) const {
        return std::hash<int>()(static_cast<int>(button));
    }
};

template <>
struct ButtonInput<MouseButton> {
   public:
    ButtonInput<MouseButton>(Entity window);

    bool just_pressed(MouseButton button) const;
    bool just_released(MouseButton button) const;
    bool pressed(MouseButton button) const;

    const spp::sparse_hash_set<MouseButton, MouseButtonHash>&
    just_pressed_buttons() const;
    const spp::sparse_hash_set<MouseButton, MouseButtonHash>&
    just_released_buttons() const;
    const spp::sparse_hash_set<MouseButton, MouseButtonHash>& pressed_buttons(
    ) const;

    bool any_just_pressed(const std::vector<MouseButton>& buttons) const;
    bool any_just_released(const std::vector<MouseButton>& buttons) const;
    bool any_pressed(const std::vector<MouseButton>& buttons) const;

    bool all_pressed(const std::vector<MouseButton>& buttons) const;

    Entity linked_window() const;

   private:
    Entity m_window;

    spp::sparse_hash_set<MouseButton, MouseButtonHash> m_just_pressed;
    spp::sparse_hash_set<MouseButton, MouseButtonHash> m_just_released;
    spp::sparse_hash_set<MouseButton, MouseButtonHash> m_pressed;

    friend void update_mouse_button_input(
        ButtonInput<MouseButton>& mouse_input, GLFWwindow* window
    );
};

void update_mouse_button_input(
    ButtonInput<MouseButton>& mouse_input, GLFWwindow* window
);
}  // namespace components
}  // namespace input
}  // namespace pixel_engine