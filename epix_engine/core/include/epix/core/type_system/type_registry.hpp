#pragma once

#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "../../api/macros.hpp"
#include "../meta/typeindex.hpp"
#include "fwd.hpp"

namespace epix::core::type_system {
struct TypeInfo {
    std::string_view name;
    size_t size;
    size_t align;

    // Mandatory operations
    void (*destroy)(void* ptr) noexcept;
    void (*copy_construct)(void* dest, const void* src);
    void (*move_construct)(void* dest, void* src);

    // Cached traits
    bool trivially_copyable;
    bool trivially_destructible;
    bool noexcept_move_constructible;

    template <typename T>
    static const TypeInfo* get_info();
};

// per-type implementations used by TypeInfo::get_info<T>()
template <typename T>
static void destroy_impl(void* p) noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
        static_cast<T*>(p)->~T();
    }
}

template <typename T>
static void copy_construct_impl(void* dest, const void* src) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dest, src, sizeof(T));
    } else if constexpr (std::is_copy_constructible_v<T>) {
        new (dest) T(*static_cast<const T*>(src));
    } else {
        // Should not be called for non-copyable types. Terminate to avoid undefined behavior.
        std::terminate();
    }
}

template <typename T>
static void move_construct_impl(void* dest, void* src) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dest, src, sizeof(T));
    } else if constexpr (std::is_nothrow_move_constructible_v<T> || std::is_move_constructible_v<T>) {
        new (dest) T(std::move(*static_cast<T*>(src)));
    } else if constexpr (std::is_copy_constructible_v<T>) {
        // Fallback to copy if move is not available but copy is
        new (dest) T(*static_cast<const T*>(src));
    } else {
        // Should not be called for types that are neither movable nor copyable.
        std::terminate();
    }
}

template <typename T>
const TypeInfo* TypeInfo::get_info() {
    static TypeInfo ti = TypeInfo{
        .name    = epix::core::meta::type_id<T>().name(),
        .size    = sizeof(T),
        .align   = alignof(T),
        .destroy = &destroy_impl<T>,
        .copy_construct =
            (std::is_trivially_copyable_v<T> || std::is_copy_constructible_v<T>) ? &copy_construct_impl<T> : nullptr,
        .move_construct =
            (std::is_trivially_copyable_v<T> || std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
                ? &move_construct_impl<T>
                : nullptr,
        .trivially_copyable          = std::is_trivially_copyable_v<T>,
        .trivially_destructible      = std::is_trivially_destructible_v<T>,
        .noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>,
    };
    return &ti;
}
EPIX_MAKE_U64_WRAPPER(TypeId)
struct TypeRegistry {
   private:
    mutable std::vector<const TypeInfo*> typeInfos;
    mutable size_t nextId = 0;
    mutable std::unordered_map<const char*, size_t> types;
    mutable std::unordered_map<std::string_view, size_t> typeViews;

    // Use a shared mutex for multiple concurrent readers and exclusive writers
    mutable std::shared_mutex mutex_;

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    template <typename T = void>
    TypeId type_id(const epix::core::meta::type_index& index = epix::core::meta::type_id<T>()) const {
        // First try with a shared (reader) lock
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (auto it = types.find(index.name().data()); it != types.end()) {
                return it->second;
            }
        }

        // Upgrade to exclusive lock to insert
        std::unique_lock<std::shared_mutex> lock(mutex_);
        // Check again in case another writer added it
        if (auto it = types.find(index.name().data()); it != types.end()) {
            return it->second;
        }

        size_t id;
        if (auto itv = typeViews.find(index.name()); itv != typeViews.end()) {
            types[index.name().data()] = itv->second;
            id                         = itv->second;
        } else {
            id = nextId++;
            typeViews.insert({index.name(), id});
            types[index.name().data()] = id;
            typeInfos.emplace_back(TypeInfo::get_info<T>());
        }
        return id;
    }
    std::optional<TypeId> type_id(const std::string_view& name) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (auto it = typeViews.find(name); it != typeViews.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    /**
     * @brief Get TypeInfo by type id.
     * Safety: The type id is get by this registry, so it must have been registered.
     */
    const TypeInfo* type_info(size_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const TypeInfo* info = typeInfos[type_id];
        return info;
    }
};
}  // namespace epix::core::type_system

namespace epix::core {
using TypeId = type_system::TypeId;  // exposing TypeId in epix::core namespace
};  // namespace epix::core

template <>
struct std::hash<epix::core::TypeId> {
    size_t operator()(const epix::core::TypeId& k) const { return std::hash<uint64_t>()(k.get()); }
};