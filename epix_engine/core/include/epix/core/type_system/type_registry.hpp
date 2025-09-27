#pragma once

#include <atomic>
#include <unordered_map>

#include "../meta/typeindex.hpp"
#include "fwd.hpp"

namespace epix::core::type_system {
struct TypeRegistry {
   private:
    mutable size_t nextId = 0;
    mutable std::unordered_map<const char*, size_t> types;
    mutable std::unordered_map<std::string_view, size_t> typeViews;

    mutable std::atomic<size_t> readers{0};
    mutable std::atomic<int> state{0};  // 0: idle, 1: waiting write

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    template <typename T = void>
    size_t type_id(const epix::core::meta::type_index& index = epix::core::meta::type_id<T>()) const {
        // change state to reading
        while (true) {
            int expected_idle = 0;
            if (state.compare_exchange_weak(expected_idle, 2)) {
                readers.fetch_add(1);
                break;
            }
        }
        // If in types
        if (auto it = types.find(index.name().data()); it != types.end()) {
            size_t id = it->second;
            readers.fetch_sub(1);
            return id;
        } else {
            // Not in types, need write
            readers.fetch_sub(1);
            state.store(1);  // lock state, waiting to write.
            size_t expected_readers = 0;
            while (readers.compare_exchange_weak(expected_readers, 0)) {
                expected_readers = 0;
            }
            if (auto itv = typeViews.find(index.name()); itv != typeViews.end()) {
                types[index.name().data()] = itv->second;
                return itv->second;
            } else {
                size_t id                  = nextId++;
                typeViews[index.name()]    = id;
                types[index.name().data()] = id;
                return id;
            }
            state.store(0);
        }
    }
};
}  // namespace epix::core::type_system