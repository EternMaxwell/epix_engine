#pragma once

#include <memory>
#include <epix/core/storage/untyped_vector.hpp>
#include <epix/core/storage/sparse_array.hpp>
#include <epix/core/storage/sparse_set.hpp>
#include <epix/core/storage/resource.hpp>
#include <epix/core/storage/table.hpp>
#include <epix/core/storage/dense.hpp>

#include <epix/core/type_registry.hpp>
#include <epix/core/component.hpp>

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

const Storage& world_storage(const World& world) noexcept;
Storage& world_storage_mut(World& world) noexcept;
}  // namespace epix::core