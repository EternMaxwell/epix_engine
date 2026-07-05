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

namespace internal {
struct type_info {
    meta::type_index index;
    StorageType type;

    template <typename T>
    static const type_info* of() {
        static const type_info info{meta::type_index(meta::type_id<T>()), storage_type_of<T>()};
        return std::addressof(info);
    }
};
}  // namespace internal

/** @brief Opaque identifier for a registered component/resource type. */
EPIX_EXPORT struct TypeId : utils::int_base<std::size_t> {
    using int_base::int_base;
    using int_base::operator std::size_t;
};

/** @brief Thread-safe registry that maps C++ types to unique type_id values.
 *  Uses a shared mutex for concurrent reads and exclusive writes.
 *  Types are lazily registered on first lookup via `type_id()`. */
EPIX_EXPORT struct TypeRegistry {
   public:
    TypeRegistry()                               = default;
    TypeRegistry(const TypeRegistry&)            = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;
    TypeRegistry(TypeRegistry&&)                 = delete;
    TypeRegistry& operator=(TypeRegistry&&)      = delete;
    ~TypeRegistry()                              = default;

    /** @brief Look up or register the type_id for type T.
     *  Thread-safe: uses shared lock for reads and upgrades to exclusive lock for writes.
     *  @tparam T The type to register.
     *  @param index The type_index to use (defaults to `meta::type_id<T>()`). */
    template <typename T = void>
    ecs::TypeId type_id(const meta::type_index& index = meta::type_id<T>()) const {
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

        std::size_t id;
        if (auto itv = typeViews.find(index.name()); itv != typeViews.end()) {
            types[index.name().data()] = itv->second;
            id                         = itv->second;
        } else {
            id = nextId++;
            typeViews.insert({index.name(), id});
            types[index.name().data()] = id;
            typeInfos.emplace_back(internal::type_info::of<T>());
        }
        return id;
    }
    /** @brief Look up a type_id by its string name.
     *  @return The type_id if the name has been registered. */
    std::optional<ecs::TypeId> type_id(const std::string_view& name) const {
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
    const meta::type_index type_index(std::size_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const internal::type_info* info = typeInfos[type_id];
        return info->index;
    }
    /** @brief Get the storage type (Table or SparseSet) for a registered type.
     *  @param type_id The id previously obtained from this registry. */
    const ecs::StorageType storage_type(std::size_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const internal::type_info* info = typeInfos[type_id];
        return info->type;
    }

    /** @brief Get the total number of registered types. */
    std::size_t count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return typeInfos.size();
    }

   private:
    mutable std::vector<const internal::type_info*> typeInfos;
    mutable std::size_t nextId = 0;
    mutable std::unordered_map<const char*, std::size_t> types;
    mutable std::unordered_map<std::string_view, std::size_t> typeViews;

    // Use a shared mutex for multiple concurrent readers and exclusive writers
    mutable std::shared_mutex mutex_;
};
}  // namespace epix::ecs