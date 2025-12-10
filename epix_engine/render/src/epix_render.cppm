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

// Re-export dependencies
export import epix_core;
export import epix_window;
export import epix_glfw;
export import epix_assets;
export import epix_image;
export import epix_transform;

// Export render headers
export {
    #include "epix/render.hpp"
}

export namespace epix::render {
    // All rendering types and functions are exported
}
