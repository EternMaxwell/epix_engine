#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#endif

namespace epix::ecs {
EPIX_EXPORT enum class StorageType : std::uint8_t {
    Table     = 0,
    SparseSet = 1,
};

/** @brief Trait to mark a component type as sparse-set stored instead of table stored.
 *  @tparam T The component type. Specialize with `std::true_type` to use sparse storage. */
EPIX_EXPORT template <typename T>
struct sparse_component : std::false_type {};

EPIX_EXPORT template <typename T>
consteval StorageType storage_type_of() {
    if constexpr (sparse_component<T>::value) {
        return StorageType::SparseSet;
    } else {
        return StorageType::Table;
    }
}
}  // namespace epix::ecs