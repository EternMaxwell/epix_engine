module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


export module epix.core:storage;

export import :storage.untyped_vector;
export import :storage.sparse_array;
export import :storage.sparse_set;
export import :storage.resource;
export import :storage.table;
export import :storage.dense;

import :type_registry;
import :component;

namespace core {
struct Storage {
    SparseSets sparse_sets;
    Tables tables;
    Resources resources;

    Storage(const std::shared_ptr<TypeRegistry>& registry)
        : sparse_sets(registry), tables(registry), resources(registry) {}

    void prepare_component(const ComponentInfo& info) {
        if (info.storage_type() == StorageType::SparseSet) {
            sparse_sets.get_or_insert(info.type_id());
        }
    }
};
}  // namespace core