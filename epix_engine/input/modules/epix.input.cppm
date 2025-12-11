/**
 * @file epix.input.cppm
 * @brief Input module for input handling
 */

export module epix.input;

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

// Module imports
#include <epix/core.hpp>

export namespace epix::input {
    // Key codes
    enum class KeyCode : uint16_t {
        // Add key codes as needed
        Unknown = 0,
        Space = 32,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Escape = 256,
        Enter,
        Tab,
        Backspace,
        // ... more keys
    };
    
    // Mouse button
    enum class MouseButton : uint8_t {
        Left = 0,
        Right = 1,
        Middle = 2,
    };
    
    // Button state
    struct ButtonInput<B> {
        std::bitset<256> pressed;
        std::bitset<256> just_pressed;
        std::bitset<256> just_released;
        
        void press(B button) {
            size_t idx = static_cast<size_t>(button);
            if (!pressed[idx]) {
                just_pressed[idx] = true;
            }
            pressed[idx] = true;
        }
        
        void release(B button) {
            size_t idx = static_cast<size_t>(button);
            if (pressed[idx]) {
                just_released[idx] = true;
            }
            pressed[idx] = false;
        }
        
        bool is_pressed(B button) const {
            return pressed[static_cast<size_t>(button)];
        }
        
        bool just_pressed(B button) const {
            return just_pressed[static_cast<size_t>(button)];
        }
        
        bool just_released(B button) const {
            return just_released[static_cast<size_t>(button)];
        }
        
        void clear() {
            just_pressed.reset();
            just_released.reset();
        }
    };
    
    using Keyboard = ButtonInput<KeyCode>;
    using Mouse = ButtonInput<MouseButton>;
    
    // Cursor position
    struct CursorPosition {
        double x = 0.0;
        double y = 0.0;
    };
    
    // Cursor moved event
    struct CursorMoved {
        double x;
        double y;
    };
    
    // Mouse wheel
    struct MouseWheel {
        double x = 0.0;
        double y = 0.0;
    };
    
    // Input plugin
    struct InputPlugin {
        void build(epix::App& app);
    };
}  // namespace epix::input
