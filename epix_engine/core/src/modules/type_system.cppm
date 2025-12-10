// Module partition for type system
// Provides type registry and type information

module;

#include <concepts>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Include macros in global fragment
#if defined(_WIN32)
#define EPIX_EXPORT __declspec(dllexport)
#define EPIX_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define EPIX_EXPORT __attribute__((visibility("default")))
#define EPIX_IMPORT __attribute__((visibility("default")))
#else
#define EPIX_EXPORT
#define EPIX_IMPORT
#endif

#if defined(EPIX_BUILD_SHARED)
#define EPIX_API EPIX_EXPORT
#elif defined(EPIX_DLL) || defined(EPIX_SHARED)
#define EPIX_API EPIX_IMPORT
#else
#define EPIX_API
#endif

export module epix.core:type_system;

import :fwd;
import :meta;

// Export wrapper utilities
export namespace epix::core::wrapper {
template <typename T>
    requires std::is_integral_v<T>
struct int_base {
public:
    using value_type = T;

    constexpr int_base(T v = 0) : value(v) {}
    constexpr T get(this int_base self) { return self.value; }
    constexpr void set(this int_base& self, T v) { self.value = v; }
    constexpr auto operator<=>(const int_base&) const = default;
    constexpr operator T() const { return value; }
    constexpr operator size_t()
        requires(!std::same_as<T, size_t>)
    {
        return static_cast<size_t>(value);
    }

protected:
    T value;
};
}  // namespace epix::core::wrapper

// Specialize std::hash for int_base
export template <typename T>
    requires std::is_integral_v<T>
struct std::hash<epix::core::wrapper::int_base<T>> {
    size_t operator()(const epix::core::wrapper::int_base<T>& v) const { 
        return std::hash<T>()(v.get()); 
    }
};

export template <typename T>
    requires std::derived_from<T, epix::core::wrapper::int_base<typename T::value_type>>
struct std::hash<T> {
    size_t operator()(const T& v) const {
        return std::hash<epix::core::wrapper::int_base<typename T::value_type>>()(v);
    }
};

// Macros for creating ID wrappers
#ifndef EPIX_MAKE_INT_WRAPPER
#define EPIX_MAKE_INT_WRAPPER(type, int_type)                        \
    struct type : public ::epix::core::wrapper::int_base<int_type> { \
       public:                                                       \
        using ::epix::core::wrapper::int_base<int_type>::int_base;   \
    };
#endif

#ifndef EPIX_MAKE_U32_WRAPPER
#define EPIX_MAKE_U32_WRAPPER(type) EPIX_MAKE_INT_WRAPPER(type, uint32_t)
#endif

#ifndef EPIX_MAKE_U64_WRAPPER
#define EPIX_MAKE_U64_WRAPPER(type) EPIX_MAKE_INT_WRAPPER(type, uint64_t)
#endif

// Storage type enumeration
export namespace epix::core {
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

export namespace epix::core::type_system {

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
    } else if constexpr (std::copy_constructible<T>) {
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
    } else if constexpr (std::is_nothrow_move_constructible_v<T> || std::move_constructible<T>) {
        new (dest) T(std::move(*static_cast<T*>(src)));
    } else if constexpr (std::copy_constructible<T>) {
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

// Expose types in epix::core namespace
export namespace epix::core {
using TypeId       = type_system::TypeId;
using TypeInfo     = type_system::TypeInfo;
using TypeRegistry = type_system::TypeRegistry;
}  // namespace epix::core
