#pragma once

#include <epix/common.hpp>
#include <epix/ecs/storage/resource.hpp>
#include <epix/ecs/storage/sparse_set.hpp>
#include <epix/ecs/storage/table.hpp>

namespace epix::ecs {

namespace internal {
struct ComponentInfo;
}  // namespace internal

EPIX_EXPORT struct Storage {
    SparseSets sparse_sets;
    Tables tables;
    Resources resources;

    Storage();
    void prepare_component(const internal::ComponentInfo& info);
};
}  // namespace epix::ecs
