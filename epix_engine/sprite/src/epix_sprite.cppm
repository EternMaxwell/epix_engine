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

// Import dependencies (full chain needed for sprite rendering)
import epix_core;
import epix_assets;
import epix_image;
import epix_transform;
import epix_render;
import epix_core_graph;

// Export sprite headers
export {
    #include "epix/sprite.hpp"
}

export namespace epix::sprite {
    // All sprite rendering types and functions are exported
}
