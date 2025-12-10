// epix_render module interface
module;

// Global module fragment
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

// Vulkan headers
#include <volk.h>

#include "epix/utils/cpp23_compat.hpp"

export module epix_render;

// Import dependencies (not re-exported)
import epix_core;
import epix_window;
import epix_glfw;
import epix_assets;
import epix_image;
import epix_transform;

// Export render headers
export {
    #include "epix/render.hpp"
}

export namespace epix::render {
    // All rendering types and functions are exported
}
