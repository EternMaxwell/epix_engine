#pragma once

#include "fwd.hpp"
#include "storage/fwd.hpp"
#include "storage/resource.hpp"
#include "storage/sparse_set.hpp"
#include "storage/table.hpp"
#include "component.hpp"

namespace epix::core {
struct Storage {
    storage::SparseSets sparse_sets;
    storage::Tables tables;
    storage::Resources resources;

    void prepare_component(const ComponentInfo& info) {
        if (info.storage_type() == StorageType::SparseSet) {
            sparse_sets.get_or_insert(info.type_id());
        }
    }
};
}