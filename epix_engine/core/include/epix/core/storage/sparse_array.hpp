#pragma once

#include <optional>
#include <vector>

#include "fwd.hpp"

namespace epix::core::storage {
template <std::convertible_to<size_t> I, typename V>
struct SparseArray {
   private:
    std::vector<std::optional<V>> values;

   public:
    bool contains(this const SparseArray& self, I index) {
        size_t idx = static_cast<size_t>(index);
        if (idx < self.values.size()) {
            return self.values[idx].has_value();
        }
        return false;
    }
    std::optional<std::reference_wrapper<V>> get_mut(this SparseArray& self, I index) {
        size_t idx = static_cast<size_t>(index);
        if (idx < self.values.size()) {
            if (self.values[idx].has_value()) {
                return self.values[idx].value();
            }
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const V>> get(this const SparseArray& self, I index) {
        size_t idx = static_cast<size_t>(index);
        if (idx < self.values.size()) {
            if (self.values[idx].has_value()) {
                return self.values[idx].value();
            }
        }
        return std::nullopt;
    }
    template <typename... Args>
    void insert(this SparseArray& self, I index, Args&&... args) {
        size_t idx = static_cast<size_t>(index);
        if (idx >= self.values.size()) {
            self.values.resize(idx + 1, std::nullopt);
        }
        self.values[idx].emplace(std::forward<Args>(args)...);
    }
    void clear(this SparseArray& self) { self.values.clear(); }
    std::optional<V> remove(this SparseArray& self, I index) {
        size_t idx           = static_cast<size_t>(index);
        std::optional<V> val = std::nullopt;
        if (idx < self.values.size()) {
            std::swap(val, self.values[idx]);
        }
        return val;
    }
};
}  // namespace epix::core::storage