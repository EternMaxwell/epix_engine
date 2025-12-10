/**
 * @file epix.core.type_system.cppm
 * @brief C++20 module interface for the type system and runtime type registry.
 *
 * This module provides TypeInfo for runtime type information and TypeRegistry
 * for managing type IDs across the engine.
 */
module;

#include <concepts>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

export module epix.core.type_system;
export import epix.core.api;
export import epix.core.meta;

export namespace epix::core {

/**
 * @brief Storage strategy for components.
 */
enum class StorageType : uint8_t {
    Table     = 0,  ///< Default: stored in archetype tables
    SparseSet = 1,  ///< Stored in sparse sets (for rare components)
};

/**
 * @brief Trait to mark a component type as sparse.
 *
 * Specialize this template for types that should use sparse storage.
 * @tparam T The component type.
 */
template <typename T>
struct sparse_component : std::false_type {};

/**
 * @brief Get the storage type for a component at compile time.
 * @tparam T The component type.
 * @return StorageType::SparseSet if sparse_component<T>::value is true,
 *         otherwise StorageType::Table.
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
 * @brief Runtime type information for a single type.
 *
 * Contains size, alignment, function pointers for construction/destruction,
 * and cached trait information needed for type-erased storage.
 */
struct TypeInfo {
    std::string_view name;        ///< Full type name
    std::string_view short_name;  ///< Shortened type name
    size_t size;                  ///< Size of the type in bytes
    size_t align;                 ///< Alignment requirement

    StorageType storage_type;  ///< Storage strategy

    // Mandatory operations
    void (*destroy)(void* ptr) noexcept;               ///< Destructor
    void (*copy_construct)(void* dest, const void* src);  ///< Copy constructor
    void (*move_construct)(void* dest, void* src);        ///< Move constructor

    // Cached traits
    bool trivially_copyable;           ///< std::is_trivially_copyable_v<T>
    bool trivially_destructible;       ///< std::is_trivially_destructible_v<T>
    bool noexcept_move_constructible;  ///< std::is_nothrow_move_constructible_v<T>

    /**
     * @brief Get the TypeInfo for type T.
     * @tparam T The type to get info for.
     * @return Pointer to static TypeInfo for T.
     */
    template <typename T>
    static const TypeInfo* get_info();
};

// Implementation helpers
namespace detail {

template <typename T>
void destroy_impl(void* p) noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
        static_cast<T*>(p)->~T();
    }
}

template <typename T>
void copy_construct_impl(void* dest, const void* src) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dest, src, sizeof(T));
    } else if constexpr (std::copy_constructible<T>) {
        new (dest) T(*static_cast<const T*>(src));
    } else {
        std::terminate();
    }
}

template <typename T>
void move_construct_impl(void* dest, void* src) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dest, src, sizeof(T));
    } else if constexpr (std::is_nothrow_move_constructible_v<T> || std::move_constructible<T>) {
        new (dest) T(std::move(*static_cast<T*>(src)));
    } else if constexpr (std::copy_constructible<T>) {
        new (dest) T(*static_cast<const T*>(src));
    } else {
        std::terminate();
    }
}

}  // namespace detail

template <typename T>
const TypeInfo* TypeInfo::get_info() {
    static TypeInfo ti = TypeInfo{
        .name         = epix::core::meta::type_id<T>::name(),
        .short_name   = epix::core::meta::type_id<T>::short_name(),
        .size         = sizeof(T),
        .align        = alignof(T),
        .storage_type = storage_for<T>(),
        .destroy      = &detail::destroy_impl<T>,
        .copy_construct =
            (std::is_trivially_copyable_v<T> || std::copy_constructible<T>) ? &detail::copy_construct_impl<T> : nullptr,
        .move_construct =
            (std::is_trivially_copyable_v<T> || std::move_constructible<T> || std::copy_constructible<T>)
                ? &detail::move_construct_impl<T>
                : nullptr,
        .trivially_copyable          = std::is_trivially_copyable_v<T>,
        .trivially_destructible      = std::is_trivially_destructible_v<T>,
        .noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>,
    };
    return &ti;
}

/**
 * @brief Strongly-typed wrapper for type IDs.
 */
struct TypeId : public epix::core::wrapper::int_base<uint64_t> {
   public:
    using epix::core::wrapper::int_base<uint64_t>::int_base;
};

/**
 * @brief Thread-safe registry for type information.
 *
 * Maps types to unique IDs and stores their TypeInfo. Used throughout
 * the ECS for type-erased component storage.
 */
struct TypeRegistry {
   private:
    mutable std::vector<const TypeInfo*> typeInfos;
    mutable size_t nextId = 0;
    mutable std::unordered_map<const char*, size_t> types;
    mutable std::unordered_map<std::string_view, size_t> typeViews;
    mutable std::shared_mutex mutex_;

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    /**
     * @brief Get or create a type ID for type T.
     * @tparam T The type to get an ID for.
     * @param index Type index (defaults to type_id<T>()).
     * @return The type ID for T.
     */
    template <typename T = void>
    TypeId type_id(const epix::core::meta::type_index& index = epix::core::meta::type_id<T>()) const {
        // First try with a shared (reader) lock
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (auto it = types.find(index.name().data()); it != types.end()) {
                return TypeId(it->second);
            }
        }

        // Upgrade to exclusive lock to insert
        std::unique_lock<std::shared_mutex> lock(mutex_);
        // Check again in case another writer added it
        if (auto it = types.find(index.name().data()); it != types.end()) {
            return TypeId(it->second);
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
        return TypeId(id);
    }

    /**
     * @brief Try to get a type ID by name.
     * @param name The full type name.
     * @return The type ID if found, nullopt otherwise.
     */
    std::optional<TypeId> type_id(const std::string_view& name) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (auto it = typeViews.find(name); it != typeViews.end()) {
            return TypeId(it->second);
        }
        return std::nullopt;
    }

    /**
     * @brief Get TypeInfo by type ID.
     * @param type_id The type ID.
     * @return Pointer to the TypeInfo.
     * @note The type ID must have been obtained from this registry.
     */
    const TypeInfo* type_info(size_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return typeInfos[type_id];
    }

    /**
     * @brief Get the number of registered types.
     */
    size_t count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return typeInfos.size();
    }
};

}  // namespace epix::core::type_system

// Re-export commonly used types at epix::core namespace level
export namespace epix::core {
using TypeId       = type_system::TypeId;
using TypeInfo     = type_system::TypeInfo;
using TypeRegistry = type_system::TypeRegistry;
}  // namespace epix::core

// Hash specialization for TypeId
export template <>
struct std::hash<epix::core::type_system::TypeId> {
    size_t operator()(const epix::core::type_system::TypeId& v) const {
        return std::hash<uint64_t>()(v.get());
    }
};
