#pragma once

#include "fwd.hpp"
#include "typeid.hpp"

namespace epix::core::meta {
struct type_index {
   private:
    std::string_view value;
    size_t hash;

   public:
    template <typename T>
    type_index(type_id<T>) : value(type_id<T>::name()), hash(type_id<T>::hash_code()) {}

    std::string_view name() const noexcept { return value; }
    size_t hash_code() const noexcept { return hash; }
};
}  // namespace epix::core::meta