#pragma once

#include "fwd.hpp"
#include "info.hpp"
#include "typeid.hpp"

namespace epix::core::meta {
struct type_index {
   private:
    const type_info* inter;

    template <typename T>
    static const type_info& get_info() {
        return type_info::of<T>();
    }

   public:
    template <typename T>
    type_index(type_id<T>) : inter(std::addressof(get_info<T>())) {}
    type_index() : inter(nullptr) {}

    auto operator<=>(const type_index& other) const noexcept {
        if (inter == other.inter) return std::strong_ordering::equal;
        if (!inter && !other.inter) return std::strong_ordering::equal;
        if (!inter) return std::strong_ordering::less;
        if (!other.inter) return std::strong_ordering::greater;
        return *inter <=> *other.inter;
    }
    bool operator==(const type_index& other) const noexcept { return (*this <=> other) == std::strong_ordering::equal; }
    std::string_view name() const noexcept { return inter->name; }
    std::string_view short_name() const noexcept { return inter->short_name; }
    size_t hash_code() const noexcept { return inter->hash; }
    const type_info& type_info() const noexcept { return *inter; }
    bool valid() const noexcept { return inter != nullptr; }
};
}  // namespace epix::core::meta

template <>
struct std::hash<epix::core::meta::type_index> {
    size_t operator()(const epix::core::meta::type_index& ti) const noexcept { return ti.hash_code(); }
};