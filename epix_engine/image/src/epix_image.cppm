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

// Re-export epix_core and epix_assets (dependencies)
export import epix_core;
export import epix_assets;

// Export image headers
export {
    #include "epix/image.hpp"
}

export namespace epix::image {
    // All image processing types and functions are exported
}
