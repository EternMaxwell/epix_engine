#pragma once

#include <functional>
#include <memory>

#include "../entities.hpp"
#include "../type_system/type_registry.hpp"
#include "dense.hpp"
#include "fwd.hpp"
#include "sparse_array.hpp"

namespace epix::core::storage {
struct ComponentSparseSet {
   private:
    Dense dense;                             // Dense storage for the actual data
    std::vector<uint32_t> entities;          // from dense index to entity index
    SparseArray<uint32_t, uint32_t> sparse;  // from entity index to dense index
   public:
    ComponentSparseSet(const type_system::TypeInfo* desc, size_t reserve_cnt = 0) : dense(desc, reserve_cnt) {}

    void clear(this ComponentSparseSet& self) {
        self.dense.clear();
        self.entities.clear();
        self.sparse.clear();
    }
    size_t size(this const ComponentSparseSet& self) { return self.dense.len(); }
    bool empty(this const ComponentSparseSet& self) { return self.size() == 0; }

    const epix::core::type_system::TypeInfo* type_info(this const ComponentSparseSet& self) {
        return self.dense.type_info();
    }

    void alloc_uninitialized(this ComponentSparseSet& self, Entity entity) {
        uint32_t dense_index = static_cast<uint32_t>(self.dense.len());
        self.dense.resize_uninitialized(self.dense.len() + 1);
        self.entities.push_back(entity.index);
        self.sparse.insert(entity.index, dense_index);
    }
    template <typename T, typename... Args>
    void emplace(this ComponentSparseSet& self, Entity entity, Tick change_tick, Args&&... args) {
        self.sparse.get(entity.index)
            .and_then([&](uint32_t& dense_index) -> std::optional<bool> {
                // Already exists, replace
                self.dense.replace<T>(dense_index, change_tick, std::forward<Args>(args)...);
                return true;
            })
            .or_else([&]() -> std::optional<bool> {
                // Doesn't exist, insert
                uint32_t dense_index = static_cast<uint32_t>(self.dense.len());
                self.dense.push<T>({change_tick, change_tick}, std::forward<Args>(args)...);
                self.entities.push_back(entity.index);
                self.sparse.insert(entity.index, dense_index);
                return std::nullopt;
            });
    }

    bool contains(this const ComponentSparseSet& self, Entity entity) { return self.sparse.contains(entity.index); }
    std::optional<const void*> get(this const ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get(dense_index);
        });
    }
    std::optional<void*> get_mut(this ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_mut(dense_index);
        });
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_as(this const ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_as<T>(dense_index);
        });
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_as_mut(this ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_as_mut<T>(dense_index);
        });
    }
    std::optional<ComponentTicks> get_ticks(this const ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_ticks(dense_index);
        });
    }
    std::optional<std::reference_wrapper<Tick>> get_added_tick(this ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_added_tick(dense_index);
        });
    }
    std::optional<std::reference_wrapper<Tick>> get_modified_tick(this ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_modified_tick(dense_index);
        });
    }
    std::optional<TickRefs> get_tick_refs(this const ComponentSparseSet& self, Entity entity) {
        return self.sparse.get(entity.index).and_then([&](uint32_t dense_index) {
            return self.dense.get_tick_refs(dense_index);
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
    std::vector<V> _dense;           // actual data
    std::vector<I> _indices;         // from dense index to index
    SparseArray<I, size_t> _sparse;  // from index to dense index

   public:
    SparseSet(size_t reserve_cnt = 0) {
        if (reserve_cnt > 0) {
            _dense.reserve(reserve_cnt);
            _indices.reserve(reserve_cnt);
        }
    }

    void clear(this SparseSet& self) {
        self._dense.clear();
        self._indices.clear();
        self._sparse.clear();
    }
    size_t size(this const SparseSet& self) { return self._dense.size(); }
    bool empty(this const SparseSet& self) { return self.size() == 0; }
    void reserve(this SparseSet& self, size_t new_cap) {
        self._dense.reserve(new_cap);
        self._indices.reserve(new_cap);
    }

    template <typename... Args>
    void emplace(this SparseSet& self, I index, Args&&... args)
        requires std::constructible_from<V, Args...>
    {
        self._sparse.get(index)
            .and_then([&](size_t dense_index) -> std::optional<bool> {
                self._dense[dense_index] = V(std::forward<Args>(args)...);
                return true;
            })
            .or_else([&]() -> std::optional<bool> {
                // Doesn't exist, insert
                I dense_index = static_cast<I>(self._dense.size());
                self._dense.emplace_back(std::forward<Args>(args)...);
                self._indices.push_back(index);
                self._sparse.insert(index, dense_index);
                return std::nullopt;
            });
    }
    bool contains(this const SparseSet& self, I index) { return self._sparse.contains(index); }
    std::optional<std::reference_wrapper<const V>> get(this const SparseSet& self, I index) {
        return self._sparse.get(index).and_then(
            [&](size_t dense_index) -> std::optional<std::reference_wrapper<const V>> {
                return std::cref(self._dense[dense_index]);
            });
    }
    std::optional<std::reference_wrapper<V>> get_mut(this SparseSet& self, I index) {
        return self._sparse.get(index).and_then([&](size_t dense_index) -> std::optional<std::reference_wrapper<V>> {
            return std::ref(self._dense[dense_index]);
        });
    }

    bool remove(this SparseSet& self, I index) {
        return self._sparse.remove(index)
            .and_then([&](size_t dense_index) -> std::optional<bool> {
                // Swap remove from dense array and indices array
                std::swap(self._dense[dense_index], self._dense.back());
                std::swap(self._indices[dense_index], self._indices.back());
                self._dense.pop_back();
                self._indices.pop_back();

                // Update sparse array for the moved index if not last
                if (dense_index < self._dense.size()) {
                    self._sparse.insert(self._indices[dense_index], dense_index);
                }

                return true;
            })
            .value_or(false);
    }

    auto values(this const SparseSet& self) -> std::span<const V> { return self._dense; }
    auto values_mut(this SparseSet& self) -> std::span<V> { return self._dense; }
    auto indices(this const SparseSet& self) -> std::span<const I> { return self._indices; }
    auto iter(this const SparseSet& self) { return std::views::zip(self._indices, self._dense); }
    auto iter_mut(this SparseSet& self) { return std::views::zip(self._indices, self._dense); }
};

struct SparseSets {
   private:
    std::shared_ptr<type_system::TypeRegistry> registry;
    SparseSet<size_t, ComponentSparseSet> sets;

   public:
    SparseSets(const std::shared_ptr<type_system::TypeRegistry>& registry) : registry(registry) {}

    size_t size(this const SparseSets& self) { return self.sets.size(); }
    bool empty(this const SparseSets& self) { return self.sets.empty(); }

    auto iter(this const SparseSets& self) { return self.sets.iter(); }
    auto iter_mut(this SparseSets& self) { return self.sets.iter_mut(); }

    std::optional<std::reference_wrapper<const ComponentSparseSet>> get(this const SparseSets& self, size_t type_id) {
        return self.sets.get(type_id);
    }
    std::optional<std::reference_wrapper<ComponentSparseSet>> get_mut(this SparseSets& self, size_t type_id) {
        return self.sets.get_mut(type_id);
    }
    void insert(this SparseSets& self, size_t type_id, ComponentSparseSet set) {
        self.sets.emplace(type_id, std::move(set));
    }
    void insert(this SparseSets& self, size_t type_id) {
        self.sets.emplace(type_id, ComponentSparseSet(self.registry->type_info(type_id)));
    }

    ComponentSparseSet& get_or_insert(this SparseSets& self, size_t type_id) {
        // This function will not throw since the type id is get from the registry, so it should have been registered.
        return self.sets.get_mut(type_id)
            .or_else([&]() -> std::optional<std::reference_wrapper<ComponentSparseSet>> {
                self.insert(type_id, ComponentSparseSet(self.registry->type_info(type_id)));
                return self.sets.get_mut(type_id);
            })
            .value()
            .get();
    }

    void clear_entities(this SparseSets& self) {
        for (auto&& [_, set] : self.sets.iter_mut()) {
            set.clear();
        }
    }
    void check_change_ticks(this SparseSets& self, Tick tick) {
        for (auto&& [_, set] : self.sets.iter_mut()) {
            set.check_change_ticks(tick);
        }
    }
};
}  // namespace epix::core::storage