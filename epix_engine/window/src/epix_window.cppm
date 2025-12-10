// epix_window module interface
module;

// Global module fragment
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "epix/utils/cpp23_compat.hpp"

export module epix_window;

// Import dependencies
import epix_core;
import epix_input;
import epix_assets;

// Export window headers
export {
    #include "epix/window.hpp"
}

export namespace epix::window {
    // All window management types and functions are exported
}
