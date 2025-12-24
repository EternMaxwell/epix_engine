#pragma once

#include <memory>

#include "component.hpp"
#include "fwd.hpp"
#include "storage/resource.hpp"
#include "storage/sparse_set.hpp"
#include "storage/table.hpp"

namespace epix::core {
struct Storage {
    storage::SparseSets sparse_sets;
    storage::Tables tables;
    storage::Resources resources;

    Storage(const std::shared_ptr<type_system::TypeRegistry>& registry)
        : sparse_sets(registry), tables(registry), resources(registry) {}

    void prepare_component(const ComponentInfo& info) {
        if (info.storage_type() == StorageType::SparseSet) {
            sparse_sets.get_or_insert(info.type_id());
        }
    }
};
}  // namespace epix::core