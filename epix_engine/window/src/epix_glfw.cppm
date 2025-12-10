// epix_glfw module interface
module;

// Global module fragment
#include <functional>
#include <memory>
#include <string>
#include <vector>

// GLFW headers
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "epix/utils/cpp23_compat.hpp"

export module epix_glfw;

// Re-export window module
export import epix_window;

// Export GLFW integration headers
export {
    #include "epix/glfw.hpp"
}

export namespace epix::glfw {
    // All GLFW integration types and functions are exported
}
