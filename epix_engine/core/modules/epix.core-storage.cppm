/**
 * @file epix.core-storage.cppm
 * @brief Storage partition for component storage systems
 */

export module epix.core:storage;

import :fwd;
import :tick;
import :type_system;
import :entities;

#include <algorithm>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

export namespace epix::core::storage {
    // Forward declarations for storage types
    template <std::convertible_to<size_t> I, typename V>
    struct SparseArray;
    
    struct UntypedVec;
    struct BitVec;
    struct Dense;
    struct ResourceData;
    struct Resources;
    struct Table;
    struct Tables;
    struct SparseSet;
    struct SparseSets;
    struct TableColumn;
}  // namespace epix::core::storage

export namespace epix::core {
    struct Storage {
        storage::Tables tables;
        storage::SparseSets sparse_sets;
        storage::Resources resources;
    };
}  // namespace epix::core
