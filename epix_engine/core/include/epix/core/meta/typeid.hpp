#pragma once

#include "fwd.hpp"
#include "info.hpp"
#include "name.hpp"

namespace epix::core::meta {
template <typename T>
struct type_id {
   public:
    static std::string_view name() { return type_info::of<T>()->name; }
    static std::string_view short_name() { return type_info::of<T>()->short_name; }
    static size_t hash_code() { return type_info::of<T>()->hash; }
};
}  // namespace epix::core::meta