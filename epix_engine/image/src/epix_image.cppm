// epix_image module interface
module;

// Global module fragment
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "epix/utils/cpp23_compat.hpp"

export module epix_image;

// Import dependencies
import epix_core;
import epix_assets;

// Export image headers
export {
    #include "epix/image.hpp"
}

export namespace epix::image {
    // All image processing types and functions are exported
}
