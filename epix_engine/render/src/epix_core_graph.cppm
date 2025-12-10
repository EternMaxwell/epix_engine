// epix_core_graph module interface
module;

// Global module fragment
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "epix/utils/cpp23_compat.hpp"

export module epix_core_graph;

// Re-export render module
export import epix_render;

// Export core_graph headers
export {
    #include "epix/core_graph.hpp"
}

export namespace epix::core_graph {
    // All render graph types and functions are exported
}
