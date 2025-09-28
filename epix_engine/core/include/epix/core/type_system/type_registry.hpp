#pragma once

#include <atomic>
#include <csetjmp>
#include <cstring>
#include <memory>
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
    bool movable;
    void (*move)(void* dest, void* src);  // If movable but this is null, that means this type is trivially movable.
    bool copyable;
    void (*copy)(void* dest,
                 const void* src);  // If copyable but this is null, that means this type is trivially copyable.
    bool destructible;
    void (*destroy)(void* ptr);              // If null, that means this type is trivially destructible.
    void (*destroy_n)(void* ptr, size_t n);  // If null, that means this type is trivially destructible.
    void (*uninitialized_move_n)(
        void* src, size_t n, void* dest);  // If movable but this is null, that means this type is trivially movable.
    void (*uninitialized_copy_n)(
        const void* src,
        size_t n,
        void* dest);  // If copyable but this is null, that means this type is trivially copyable.

    template <typename T>
    static TypeInfo make_info() {
        return TypeInfo {
            .name = epix::core::meta::type_id<T>{}.name(), .size = sizeof(T), .align = alignof(T),
            .movable      = std::is_move_constructible_v<T>,
            .move         = std::is_trivially_move_constructible<T>::value
                                ? nullptr
                                : (std::is_move_constructible_v<T>
                                       ? [](void* dest, void* src) { new (dest) T(std::move(*static_cast<T*>(src))); }
                                       : nullptr),
            .copyable     = std::is_copy_constructible_v<T>,
            .copy         = std::is_trivially_copy_constructible<T>::value
                                ? nullptr
                                : (std::is_copy_constructible_v<T>
                                       ? [](void* dest, const void* src) { new (dest) T(*static_cast<const T*>(src)); }
                                       : nullptr),
            .destructible = std::is_destructible_v<T>,
            .destroy      = std::is_trivially_destructible_v<T>
                                ? nullptr
                                : (std::is_destructible_v<T> ? [](void* ptr) { static_cast<T*>(ptr)->~T(); } : nullptr),
            .destroy_n    = std::is_trivially_destructible_v<T>
                                ? nullptr
                                : [](void* ptr, size_t n) {
                                      T* typed_ptr = static_cast<T*>(ptr);
                                      for (size_t i = 0; i < n; i++) {
                                          typed_ptr[i].~T();
                                      }
                                  },
            .uninitialized_move_n = std::is_trivially_move_constructible<T>::value
                                        ? nullptr
                                        : [](void* src, size_t n, void* dest) {
                                              T* src_t  = static_cast<T*>(src);
                                              T* dest_t = static_cast<T*>(dest);
                                              std::uninitialized_move_n(src_t, n, dest_t);
                                          },
            .uninitialized_copy_n = std::is_trivially_copy_constructible<T>::value
                                        ? nullptr
                                        : [](const void* src, size_t n, void* dest) {
                                              const T* src_t = static_cast<const T*>(src);
                                              T* dest_t     = static_cast<T*>(dest);
                                              std::uninitialized_copy_n(src_t, n, dest_t);
                                          },
        };
    }
};
struct TypeRegistry {
   private:
    mutable std::vector<TypeInfo> typeInfos;
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
                typeInfos.push_back(TypeInfo::make_info<T>());
            }
            unlock_write();
            return id;
        }
    }
    TypeInfo type_info(size_t type_id) const {
        lock_read();
        TypeInfo info = typeInfos[type_id];
        unlock_read();
        return info;
    }
};
}  // namespace epix::core::type_system