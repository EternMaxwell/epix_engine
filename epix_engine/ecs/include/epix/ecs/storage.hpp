#pragma once

#include <epix/ecs/component.hpp>
#include <epix/ecs/storage/dense.hpp>
#include <epix/ecs/storage/resource.hpp>
#include <epix/ecs/storage/sparse_array.hpp>
#include <epix/ecs/storage/sparse_set.hpp>
#include <epix/ecs/storage/storage_type.hpp>
#include <epix/ecs/storage/table.hpp>
#include <epix/ecs/storage/untyped_vector.hpp>

namespace epix::ecs::internal {
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
}  // namespace epix::ecs::internal