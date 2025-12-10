// epix_assets module interface
module;

// Global module fragment
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "epix/utils/cpp23_compat.hpp"

export module epix_assets;

// Export assets headers
export {
    #include "epix/assets.hpp"
}

export namespace epix::assets {
    // All asset management types and functions are exported
}
