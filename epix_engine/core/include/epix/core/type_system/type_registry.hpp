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

namespace epix::core {
enum class StorageType : uint8_t {
    Table     = 0,  // default stored in tables
    SparseSet = 1,
};
template <typename T>
struct sparse_component : std::false_type {};

template <typename T>
consteval StorageType storage_for() {
    if constexpr (sparse_component<T>::value) {
        return StorageType::SparseSet;
    } else {
        return StorageType::Table;
    }
}
}  // namespace epix::core

namespace epix::core::type_system {
struct TypeInfo {
    epix::core::meta::type_index type_index;
    StorageType storage_type;

    template <typename T>
    static const TypeInfo* get_info();
};

template <typename T>
const TypeInfo* TypeInfo::get_info() {
    static TypeInfo ti = TypeInfo{
        .type_index   = epix::core::meta::type_id<T>(),
        .storage_type = storage_for<T>(),
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
    size_t count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return typeInfos.size();
    }
};
}  // namespace epix::core::type_system

namespace epix::core {
using TypeId       = type_system::TypeId;  // exposing TypeId in epix::core namespace
using TypeInfo     = type_system::TypeInfo;
using TypeRegistry = type_system::TypeRegistry;
};  // namespace epix::core