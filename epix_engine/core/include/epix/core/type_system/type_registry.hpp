#pragma once

#include <unordered_map>

#include "fwd.hpp"
#include "typeindex.hpp"

namespace epix::core::type_system {
struct TypeRegistry {
   private:
    size_t nextId = 0;
    std::unordered_map<const char*, size_t> types;
    std::unordered_map<std::string_view, size_t> typeViews;

   public:
    TypeRegistry()  = default;
    ~TypeRegistry() = default;

    template <typename T = void>
    size_t type_id(const epix::core::meta::type_index& index = epix::core::meta::type_id<T>()) {
        // If in types
        if (auto it = types.find(index.name().data()); it != types.end()) {
            return it->second;
        } else if (auto itv = typeViews.find(index.name()); itv != typeViews.end()) {
            types[index.name().data()] = itv->second;
            return itv->second;
        } else {
            size_t id                  = nextId++;
            types[index.name().data()] = id;
            typeViews[index.name()]    = id;
            return id;
        }
    }
};
}  // namespace epix::core::type_system