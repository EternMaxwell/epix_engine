// epix_sprite module interface
module;

// Global module fragment
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "epix/utils/cpp23_compat.hpp"

export module epix_sprite;

// Re-export dependencies
export import epix_core;
export import epix_assets;
export import epix_image;
export import epix_transform;
export import epix_render;
export import epix_core_graph;

// Export sprite headers
export {
    #include "epix/sprite.hpp"
}

export namespace epix::sprite {
    // All sprite rendering types and functions are exported
}
