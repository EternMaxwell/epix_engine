// epix_input module interface
module;

// Global module fragment
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

#include "epix/utils/cpp23_compat.hpp"

export module epix_input;

// Export input headers
export {
    #include "epix/input.hpp"
}

export namespace epix::input {
    // All input types and functions are exported
}
