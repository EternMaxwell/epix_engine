module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


export module epix.core:storage.sparse_array;

namespace core {
template <std::convertible_to<std::size_t> I, typename V>
struct SparseArray {
   private:
    std::vector<std::optional<V>> values;

   public:
    bool contains(this const SparseArray& self, I index) {
        std::size_t idx = static_cast<std::size_t>(index);
        if (idx < self.values.size()) {
            return self.values[idx].has_value();
        }
        return false;
    }
    std::optional<std::reference_wrapper<V>> get_mut(this SparseArray& self, I index) {
        std::size_t idx = static_cast<std::size_t>(index);
        if (idx < self.values.size()) {
            if (self.values[idx].has_value()) {
                return self.values[idx].value();
            }
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const V>> get(this const SparseArray& self, I index) {
        std::size_t idx = static_cast<std::size_t>(index);
        if (idx < self.values.size()) {
            if (self.values[idx].has_value()) {
                return self.values[idx].value();
            }
        }
        return std::nullopt;
    }
    template <typename... Args>
    void insert(this SparseArray& self, I index, Args&&... args) {
        std::size_t idx = static_cast<std::size_t>(index);
        if (idx >= self.values.size()) {
            self.values.resize(idx + 1, std::nullopt);
        }
        self.values[idx].emplace(std::forward<Args>(args)...);
    }
    void clear(this SparseArray& self) { self.values.clear(); }
    std::optional<V> remove(this SparseArray& self, I index) {
        std::size_t idx      = static_cast<std::size_t>(index);
        std::optional<V> val = std::nullopt;
        if (idx < self.values.size()) {
            std::swap(val, self.values[idx]);
        }
        return val;
    }
};
}  // namespace core