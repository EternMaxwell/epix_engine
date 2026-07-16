#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <epix/utils.hpp>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#endif

#include <epix/ecs/storage/storage_type.hpp>

namespace epix::ecs {

/** @brief Opaque identifier for a registered component/resource type. */
EPIX_EXPORT struct TypeId : utils::int_base<std::size_t> {
    using int_base::int_base;
    using int_base::operator std::size_t;
};
}  // namespace epix::ecs