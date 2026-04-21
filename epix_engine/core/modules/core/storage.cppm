module;

#ifndef EPIX_IMPORT_STD
#include <memory>
#endif
export module epix.core:storage;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import :storage.untyped_vector;
export import :storage.sparse_array;
export import :storage.sparse_set;
export import :storage.resource;
export import :storage.table;
export import :storage.dense;

import :type_registry;
import :component;

namespace epix::core {
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

const Storage& world_storage(const World& world);
Storage& world_storage_mut(World& world);
}  // namespace core