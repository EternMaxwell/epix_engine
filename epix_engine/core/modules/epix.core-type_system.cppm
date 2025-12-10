/**
 * @file epix.core-type_system.cppm
 * @brief Type system partition for type registration and metadata
 */

export module epix.core:type_system;

import :api;
import :meta;

#include <concepts>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

export namespace epix::core {
    /**
     * Enum for component storage type
     */
    enum class StorageType : uint8_t {
        Table     = 0,  // default: stored in tables
        SparseSet = 1,  // stored in sparse sets
    };
    
    /**
     * Trait for marking components as sparse
     */
    template <typename T>
    struct sparse_component : std::false_type {};
    
    /**
     * Determine storage type for a component at compile time
     */
    template <typename T>
    consteval StorageType storage_for() {
        if constexpr (sparse_component<T>::value) {
            return StorageType::SparseSet;
        } else {
            return StorageType::Table;
        }
    }
}  // namespace epix::core

export namespace epix::core::type_system {
    
    /**
     * Runtime type information
     */
    struct TypeInfo {
        std::string_view name;
        std::string_view short_name;
        size_t size;
        size_t align;

        StorageType storage_type;

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

    /**
     * Destroy implementation for any type
     */
    template <typename T>
    static void destroy_impl(void* p) noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            static_cast<T*>(p)->~T();
        }
    }

    /**
     * Copy construction implementation
     */
    template <typename T>
    static void copy_construct_impl(void* dest, const void* src) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(dest, src, sizeof(T));
        } else if constexpr (std::copy_constructible<T>) {
            new (dest) T(*static_cast<const T*>(src));
        } else {
            // Should not be called for non-copyable types
            std::terminate();
        }
    }

    /**
     * Move construction implementation
     */
    template <typename T>
    static void move_construct_impl(void* dest, void* src) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(dest, src, sizeof(T));
        } else if constexpr (std::is_nothrow_move_constructible_v<T> || std::move_constructible<T>) {
            new (dest) T(std::move(*static_cast<T*>(src)));
        } else if constexpr (std::copy_constructible<T>) {
            // Fallback to copy if move is not available
            new (dest) T(*static_cast<const T*>(src));
        } else {
            // Should not be called for non-movable non-copyable types
            std::terminate();
        }
    }

    /**
     * Get TypeInfo for a specific type
     */
    template <typename T>
    const TypeInfo* TypeInfo::get_info() {
        static TypeInfo ti = TypeInfo{
            .name         = epix::core::meta::type_id<T>().name(),
            .short_name   = epix::core::meta::type_id<T>().short_name(),
            .size         = sizeof(T),
            .align        = alignof(T),
            .storage_type = storage_for<T>(),
            .destroy      = &destroy_impl<T>,
            .copy_construct =
                (std::is_trivially_copyable_v<T> || std::copy_constructible<T>) ? &copy_construct_impl<T> : nullptr,
            .move_construct = (std::is_trivially_copyable_v<T> || std::move_constructible<T> || std::copy_constructible<T>)
                                  ? &move_construct_impl<T>
                                  : nullptr,
            .trivially_copyable          = std::is_trivially_copyable_v<T>,
            .trivially_destructible      = std::is_trivially_destructible_v<T>,
            .noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>,
        };
        return &ti;
    }
    
    /**
     * Type identifier - unique ID for each registered type
     */
    EPIX_MAKE_U64_WRAPPER(TypeId)
    
    /**
     * Central registry for all types in the system
     * Thread-safe type registration and lookup
     */
    struct TypeRegistry {
       private:
        mutable std::vector<const TypeInfo*> typeInfos;
        mutable size_t nextId = 0;
        mutable std::unordered_map<const char*, size_t> types;
        mutable std::unordered_map<std::string_view, size_t> typeViews;

        // Shared mutex for concurrent access
        mutable std::shared_mutex mutex_;

       public:
        TypeRegistry()  = default;
        ~TypeRegistry() = default;

        /**
         * Get or register a type ID
         */
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
        
        /**
         * Get type ID by name (if already registered)
         */
        std::optional<TypeId> type_id(const std::string_view& name) const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (auto it = typeViews.find(name); it != typeViews.end()) {
                return it->second;
            }
            return std::nullopt;
        }
        
        /**
         * Get TypeInfo for a registered type ID
         */
        const TypeInfo* type_info(size_t type_id) const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            const TypeInfo* info = typeInfos[type_id];
            return info;
        }
        
        /**
         * Get number of registered types
         */
        size_t count() const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return typeInfos.size();
        }
    };
}  // namespace epix::core::type_system

// Re-export TypeId in core namespace
export namespace epix::core {
    using TypeId       = type_system::TypeId;
    using TypeInfo     = type_system::TypeInfo;
    using TypeRegistry = type_system::TypeRegistry;
}  // namespace epix::core
