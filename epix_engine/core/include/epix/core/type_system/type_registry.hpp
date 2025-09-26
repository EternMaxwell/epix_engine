#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "../meta/typeindex.hpp"
#include "fwd.hpp"

namespace epix::core::type_system {
struct TypeRegistry {
   private:
    mutable size_t nextId = 0;
    mutable std::unordered_map<const char*, size_t> types;

    mutable std::unordered_map<std::string_view, size_t> typeViews;

    mutable std::vector<std::pair<const char*, size_t>> cache;
    mutable std::mutex mutex;

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    template <typename T = void>
    size_t type_id(const epix::core::meta::type_index& index = epix::core::meta::type_id<T>()) const {
        // If in types
        if (auto it = types.find(index.name().data()); it != types.end()) {
            // The desired path, no lock, should be fast.
            return it->second;
        } else {
            std::lock_guard lock(mutex);
            if (auto itv = typeViews.find(index.name()); itv != typeViews.end()) {
                types[index.name().data()] = itv->second;
                return itv->second;
            } else {
                size_t id = nextId++;
                cache.emplace_back(index.name().data(), id);
                typeViews[index.name()] = id;
                return id;
            }
        }
    }

    void flush() {
        // This function changes the types map, so it should be called when no other threads are using type_id, no need
        // to lock mutex
        for (auto& [name, id] : cache) {
            types[name] = id;
        }
        cache.clear();
    }
};
}  // namespace epix::core::type_system