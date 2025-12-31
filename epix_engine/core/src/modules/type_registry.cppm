module;

export module epix.core:type_registry;

import std;
import epix.meta;

import :utils;

namespace core {
enum class StorageType : std::uint8_t {
    Table     = 0,  // default stored in tables
    SparseSet = 1,
};
export template <typename T>
struct sparse_component : std::false_type {};

template <typename T>
consteval StorageType storage_for() {
    if constexpr (sparse_component<T>::value) {
        return StorageType::SparseSet;
    } else {
        return StorageType::Table;
    }
}
export struct TypeId : core::int_base<std::uint64_t> {
    using int_base::int_base;
};
struct TypeInfo {
    meta::type_index type_index;
    StorageType storage_type;

    template <typename T>
    static const TypeInfo* get_info() {
        static TypeInfo info{meta::type_id<T>(), storage_for<T>()};
        return &info;
    }
};
export struct TypeRegistry {
   private:
    mutable std::vector<const TypeInfo*> typeInfos;
    mutable std::size_t nextId = 0;
    mutable std::unordered_map<const char*, std::size_t> types;
    mutable std::unordered_map<std::string_view, std::size_t> typeViews;

    // Use a shared mutex for multiple concurrent readers and exclusive writers
    mutable std::shared_mutex mutex_;

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    template <typename T = void>
    TypeId type_id(const meta::type_index& index = meta::type_id<T>()) const {
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
    const meta::type_index type_index(std::size_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const TypeInfo* info = typeInfos[type_id];
        return info->type_index;
    }
    const StorageType storage_type(std::size_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const TypeInfo* info = typeInfos[type_id];
        return info->storage_type;
    }

    std::size_t count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return typeInfos.size();
    }
};
}  // namespace core