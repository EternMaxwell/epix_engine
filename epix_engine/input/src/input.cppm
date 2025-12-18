// Module interface unit for epix.input
// This file is only compiled when EPIX_ENABLE_MODULES=ON

module;

// Import dependencies in global module fragment
#include <epix/core.hpp>
#include <epix/input.hpp>

export module epix.input;

// Re-export the input namespace
export namespace epix::input {
    // Enums
    using ::epix::input::KeyCode;
    using ::epix::input::MouseButton;
    
    // Events
    using ::epix::input::events::KeyInput;
    using ::epix::input::events::MouseButtonInput;
    using ::epix::input::events::MouseMove;
    using ::epix::input::events::MouseScroll;
    
    // Button input
    using ::epix::input::ButtonInput;
    
    // Plugin
    using ::epix::input::InputPlugin;
    
    // Functions
    using ::epix::input::log_inputs;
}

// Make input available in epix namespace (matching header behavior)
export namespace epix {
    // Re-export prelude items to match header
    using ::epix::input::prelude::ButtonInput;
    using ::epix::input::prelude::InputPlugin;
    using ::epix::input::prelude::KeyCode;
    using ::epix::input::prelude::MouseButton;
}
