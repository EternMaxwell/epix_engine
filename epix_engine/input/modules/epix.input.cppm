/**
 * @file epix.input.cppm
 * @brief Input module for keyboard and mouse input handling
 */

export module epix.input;

#include <epix/core.hpp>
#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <vector>

export namespace epix::input {
    // Key codes and mouse buttons from enums.hpp
    enum class KeyCode {
        Unknown = -1,
        Space = 32,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        // Add more as needed
    };
    
    enum class MouseButton {
        Left = 0,
        Right = 1,
        Middle = 2,
    };
    
    // Button input template
    template <typename T>
    struct ButtonInput {};
    
    // Specialized for KeyCode
    template <>
    struct ButtonInput<KeyCode> {
       public:
        ButtonInput<KeyCode>() = default;

        bool just_pressed(KeyCode key) const;
        bool just_released(KeyCode key) const;
        bool pressed(KeyCode key) const;

        auto just_pressed_keys() const;
        auto just_released_keys() const;
        auto pressed_keys() const;

        bool any_just_pressed(const std::vector<KeyCode>& keys) const;
        bool any_just_released(const std::vector<KeyCode>& keys) const;
        bool any_pressed(const std::vector<KeyCode>& keys) const;
        bool all_pressed(const std::vector<KeyCode>& keys) const;

        static void collect_events(/* params */);

       private:
        std::unordered_set<KeyCode> m_just_pressed;
        std::unordered_set<KeyCode> m_just_released;
        std::unordered_set<KeyCode> m_pressed;
    };
    
    // Specialized for MouseButton
    template <>
    struct ButtonInput<MouseButton> {
       public:
        ButtonInput<MouseButton>() = default;

        bool just_pressed(MouseButton button) const;
        bool just_released(MouseButton button) const;
        bool pressed(MouseButton button) const;

        auto just_pressed_buttons() const;
        auto just_released_buttons() const;
        auto pressed_buttons() const;

        bool any_just_pressed(const std::vector<MouseButton>& buttons) const;
        bool any_just_released(const std::vector<MouseButton>& buttons) const;
        bool any_pressed(const std::vector<MouseButton>& buttons) const;
        bool all_pressed(const std::vector<MouseButton>& buttons) const;

        static void collect_events(/* params */);

       private:
        std::unordered_set<MouseButton> m_just_pressed;
        std::unordered_set<MouseButton> m_just_released;
        std::unordered_set<MouseButton> m_pressed;
    };
    
    // Input events
    namespace events {
        struct KeyInput {
            epix::Entity window;
            KeyCode key_code;
            bool pressed;
        };
        
        struct MouseButtonInput {
            epix::Entity window;
            MouseButton button;
            bool pressed;
        };
        
        struct MouseMotion {
            epix::Entity window;
            double delta_x;
            double delta_y;
        };
        
        struct MouseWheel {
            epix::Entity window;
            double delta_x;
            double delta_y;
        };
    }  // namespace events
    
    // Input plugin
    struct InputPlugin {
        void build(epix::App& app);
    };
    
}  // namespace epix::input
