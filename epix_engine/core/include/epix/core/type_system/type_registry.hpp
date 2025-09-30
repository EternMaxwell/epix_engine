#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

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
    } else {
        new (dest) T(*static_cast<const T*>(src));
    }
}

template <typename T>
static void move_construct_impl(void* dest, void* src) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dest, src, sizeof(T));
    } else {
        new (dest) T(std::move(*static_cast<T*>(src)));
    }
}

template <typename T>
const TypeInfo* TypeInfo::get_info() {
    static TypeInfo ti = TypeInfo{
        .name                        = epix::core::meta::type_id<T>().name(),
        .size                        = sizeof(T),
        .align                       = alignof(T),
        .destroy                     = &destroy_impl<T>,
        .copy_construct              = &copy_construct_impl<T>,
        .move_construct              = &move_construct_impl<T>,
        .trivially_copyable          = std::is_trivially_copyable_v<T>,
        .trivially_destructible      = std::is_trivially_destructible_v<T>,
        .noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>,
    };
    return &ti;
}
struct TypeRegistry {
   private:
    mutable std::vector<const TypeInfo*> typeInfos;
    mutable size_t nextId = 0;
    mutable std::unordered_map<const char*, size_t> types;
    mutable std::unordered_map<std::string_view, size_t> typeViews;

    mutable std::atomic<size_t> readers{0};
    mutable std::atomic<int> state{0};  // 0: idle, 1: waiting write

    void lock_read() const {
        while (true) {
            int expected_idle = 0;
            if (state.compare_exchange_weak(expected_idle, 0)) {
                readers.fetch_add(1);
                break;
            }
        }
    }
    void unlock_read() const { readers.fetch_sub(1); }
    void lock_write() const {
        state.store(1);  // lock state, waiting to write.
        size_t expected_readers = 0;
        while (readers.compare_exchange_weak(expected_readers, 0)) {
            expected_readers = 0;
        }
    }
    void unlock_write() const { state.store(0); }

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    template <typename T = void>
    size_t type_id(const epix::core::meta::type_index& index = epix::core::meta::type_id<T>()) const {
        // change state to reading
        lock_read();
        // If in types
        if (auto it = types.find(index.name().data()); it != types.end()) {
            size_t id = it->second;
            unlock_read();
            return id;
        } else {
            // Not in types, need write
            unlock_read();
            lock_write();
            size_t id;
            if (auto itv = typeViews.find(index.name()); itv != typeViews.end()) {
                types[index.name().data()] = itv->second;
                id                         = itv->second;
            } else {
                id                         = nextId++;
                typeViews[index.name()]    = id;
                types[index.name().data()] = id;
                typeInfos.push_back(TypeInfo::get_info<T>());
            }
            unlock_write();
            return id;
        }
    }
    const TypeInfo* type_info(size_t type_id) const {
        lock_read();
        const TypeInfo* info = typeInfos[type_id];
        unlock_read();
        return info;
    }
};
}  // namespace epix::core::type_system