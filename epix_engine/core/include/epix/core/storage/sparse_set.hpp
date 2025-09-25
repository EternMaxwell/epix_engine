#pragma once

#include "../entities.hpp"
#include "dense.hpp"
#include "fwd.hpp"
#include "sparse_array.hpp"

namespace epix::core::storage {
template <typename T>
struct ComponentSparseSet {
   private:
    Dense<T> dense;                          // Dense storage for the actual data
    std::vector<uint32_t> entities;          // from dense index to entity index
    SparseArray<uint32_t, uint32_t> sparse;  // from entity index to dense index
   public:
    ComponentSparseSet() = default;

    void clear(this ComponentSparseSet& self) {
        self.dense.clear();
        self.entities.clear();
        self.sparse.clear();
    }
    size_t size(this const ComponentSparseSet& self) { return self.dense.len(); }
    bool empty(this const ComponentSparseSet& self) { return self.size() == 0; }

    template <typename... Args>
    void emplace(this ComponentSparseSet& self, Entity entity, Tick change_tick, Args&&... args) {
        self.sparse.get(entity.index)
            .and_then([&](uint32_t& dense_index) -> std::optional<bool> {
                // Already exists, replace
                self.dense.replace(dense_index, change_tick, std::forward<Args>(args)...);
                return true;
            })
            .or_else([&]() -> std::optional<bool> {
                // Doesn't exist, insert
                uint32_t dense_index = static_cast<uint32_t>(self.dense.len());
                self.dense.push({change_tick, change_tick}, std::forward<Args>(args)...);
                self.entities.push_back(entity.index);
                self.sparse.insert(entity.index, dense_index);
                return std::nullopt;
            });
    }

    bool contains(this const ComponentSparseSet& self, Entity entity) { return self.sparse.contains(entity.index); }
    std::optional<std::reference_wrapper<const T>> get(this const ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_data(dense_index);
        });
    }
    std::optional<std::reference_wrapper<T>> get_mut(this ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_data_mut(dense_index);
        });
    }
    std::optional<ComponentTicks> get_ticks(this const ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_ticks(dense_index);
        });
    }
    std::optional<std::reference_wrapper<const Tick>> get_added_tick(this const ComponentSparseSet& self,
                                                                     Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_added_tick(dense_index);
        });
    }
    std::optional<std::reference_wrapper<const Tick>> get_modified_tick(this const ComponentSparseSet& self,
                                                                        Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_modified_tick(dense_index);
        });
    }

    bool remove(this ComponentSparseSet& self, Entity entity) {
        return self.sparse.remove(entity.index)
            .and_then([&](uint32_t dense_index) -> std::optional<bool> {
                // Swap remove from dense array and entities array
                uint32_t last_entity_index = self.entities.back();
                self.dense.swap_remove(dense_index);
                std::swap(self.entities[dense_index], self.entities.back());
                self.entities.pop_back();

                // Update sparse array for the moved entity if not last
                if (dense_index < self.dense.len()) {
                    self.sparse.insert(self.entities[dense_index], dense_index);
                }

                return true;
            })
            .value_or(false);
    }
    void check_change_ticks(this ComponentSparseSet& self, Tick tick) { self.dense.check_change_ticks(tick); }
};

template <typename I, typename V>
struct SparseSet {
   private:
    std::vector<V> dense;      // actual data
    std::vector<I> indices;    // from dense index to index
    SparseArray<I, I> sparse;  // from index to dense index

   public:
    SparseSet() = default;

    void clear(this SparseSet& self) {
        self.dense.clear();
        self.indices.clear();
        self.sparse.clear();
    }
    size_t size(this const SparseSet& self) { return self.dense.size(); }
    bool empty(this const SparseSet& self) { return self.size() == 0; }

    template <typename... Args>
    void emplace(this SparseSet& self, I index, Args&&... args) {
        self.sparse.get(index)
            .and_then([&](I& dense_index) -> std::optional<bool> {
                self.dense[dense_index] = V(std::forward<Args>(args)...);
                return true;
            })
            .or_else([&]() -> std::optional<bool> {
                // Doesn't exist, insert
                I dense_index = static_cast<I>(self.dense.size());
                self.dense.emplace_back(std::forward<Args>(args)...);
                self.indices.push_back(index);
                self.sparse.insert(index, dense_index);
                return std::nullopt;
            });
    }
    bool contains(this const SparseSet& self, I index) { return self.sparse.contains(index); }
    std::optional<std::reference_wrapper<const V>> get(this const SparseSet& self, I index) {
        return self.sparse.get(index).and_then([&](I dense_index) { return self.dense.get_data(dense_index); });
    }
    std::optional<std::reference_wrapper<V>> get_mut(this SparseSet& self, I index) {
        return self.sparse.get(index).and_then([&](I dense_index) { return self.dense.get_data_mut(dense_index); });
    }

    bool remove(this SparseSet& self, I index) {
        return self.sparse.remove(index)
            .and_then([&](I dense_index) -> std::optional<bool> {
                // Swap remove from dense array and indices array
                I last_index = self.indices.back();
                self.dense.swap_remove(dense_index);
                std::swap(self.indices[dense_index], self.indices.back());
                self.indices.pop_back();

                // Update sparse array for the moved index if not last
                if (dense_index < self.dense.size()) {
                    self.sparse.insert(self.indices[dense_index], dense_index);
                }

                return true;
            })
            .value_or(false);
    }

    auto values(this const SparseSet& self) -> std::span<const V> { return self.dense; }
    auto values_mut(this SparseSet& self) -> std::span<V> { return self.dense; }
    auto indices(this const SparseSet& self) -> std::span<const I> { return self.indices; }
    auto iter(this const SparseSet& self) { return std::views::zip(self.indices, self.dense); }
    auto iter_mut(this SparseSet& self) { return std::views::zip(self.indices, self.dense); }
};

struct SparseSets {
   private:
    SparseSet<size_t, std::shared_ptr<void>> sets;

   public:
    
};
}  // namespace epix::core::storage