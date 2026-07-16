#pragma once

#include <epix/ecs/component.hpp>
#include <epix/ecs/storage/dense.hpp>
#include <epix/ecs/storage/resource.hpp>
#include <epix/ecs/storage/sparse_array.hpp>
#include <epix/ecs/storage/sparse_set.hpp>
#include <epix/ecs/storage/storage_type.hpp>
#include <epix/ecs/storage/table.hpp>
#include <epix/ecs/storage/untyped_vector.hpp>

namespace epix::ecs {
EPIX_EXPORT struct Storage {
    SparseSets sparse_sets;
    Tables tables;
    Resources resources;

    Storage() : sparse_sets(), tables(), resources() {}

    void prepare_component(const internal::ComponentInfo& info) {
        if (info.storage_type() == StorageType::SparseSet) {
            sparse_sets.get_or_insert(info);
        }
    }
};
}  // namespace epix::ecs