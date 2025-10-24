#pragma once

#include "fwd.hpp"
#include "typeid.hpp"

namespace epix::core::meta {
struct type_index {
   private:
    struct Internal {
        std::string_view name;
        std::string_view short_name;
        size_t hash;
    };

    const Internal* inter;

    template <typename T>
    const Internal* get_internal() const {
        static Internal internal = {type_id<T>::name(), type_id<T>::short_name(), type_id<T>::hash_code()};
        return &internal;
    }

   public:
    template <typename T>
    type_index(type_id<T>) : inter(get_internal<T>()) {}
    type_index() : inter(nullptr) {}

    bool operator==(const type_index& other) const noexcept {
        return inter == other.inter || inter->name == other.inter->name;
    }
    bool operator!=(const type_index& other) const noexcept { return !(*this == other); }
    std::string_view name() const noexcept { return inter->name; }
    std::string_view short_name() const noexcept { return inter->short_name; }
    size_t hash_code() const noexcept { return inter->hash; }
    bool valid() const noexcept { return inter != nullptr; }
};
}  // namespace epix::core::meta

template <>
struct std::hash<epix::core::meta::type_index> {
    size_t operator()(const epix::core::meta::type_index& ti) const noexcept { return ti.hash_code(); }
};