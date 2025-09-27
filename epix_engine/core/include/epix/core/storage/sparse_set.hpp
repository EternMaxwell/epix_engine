#pragma once

#include <functional>
#include <memory>

#include "../entities.hpp"
#include "../type_system/type_registry.hpp"
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
struct ComponentSparseSetInterface {
   private:
    struct Functor {
        void (*clear)(void*);
        size_t (*size)(const void*);
        bool (*empty)(const void*);
        void (*check_change_ticks)(void*, Tick);
        bool (*remove)(void*, Entity);
        bool (*contains)(const void*, Entity);
        std::optional<const void*> (*get)(const void*, Entity);
        std::optional<void*> (*get_mut)(void*, Entity);
        std::optional<ComponentTicks> (*get_ticks)(const void*, Entity);
        std::optional<std::reference_wrapper<const Tick>> (*get_added_tick)(const void*, Entity);
        std::optional<std::reference_wrapper<const Tick>> (*get_modified_tick)(const void*, Entity);
    }* functor;
    std::unique_ptr<void, void (*)(void*)> data;

    template <typename T>
    Functor* get_functor() {
        static Functor f = {
            // clear
            [](void* ptr) { static_cast<ComponentSparseSet<T>*>(ptr)->clear(); },
            // size
            [](const void* ptr) { return static_cast<const ComponentSparseSet<T>*>(ptr)->size(); },
            // empty
            [](const void* ptr) { return static_cast<const ComponentSparseSet<T>*>(ptr)->empty(); },
            // check_change_ticks
            [](void* ptr, Tick tick) { static_cast<ComponentSparseSet<T>*>(ptr)->check_change_ticks(tick); },
            // remove
            [](void* ptr, Entity entity) { return static_cast<ComponentSparseSet<T>*>(ptr)->remove(entity); },
            // contains
            [](const void* ptr, Entity entity) {
                return static_cast<const ComponentSparseSet<T>*>(ptr)->contains(entity);
            },
            // get
            [](const void* ptr, Entity entity) -> std::optional<const void*> {
                return static_cast<const ComponentSparseSet<T>*>(ptr)->get(entity).transform(
                    [](std::reference_wrapper<const T> ref) -> const void* { return &ref.get(); });
            },
            // get_mut
            [](void* ptr, Entity entity) -> std::optional<void*> {
                return static_cast<ComponentSparseSet<T>*>(ptr)->get_mut(entity).transform(
                    [](std::reference_wrapper<T> ref) -> void* { return &ref.get(); });
            },
            // get_ticks
            [](const void* ptr, Entity entity) {
                return static_cast<const ComponentSparseSet<T>*>(ptr)->get_ticks(entity);
            },
            // get_added_tick
            [](const void* ptr, Entity entity) {
                return static_cast<const ComponentSparseSet<T>*>(ptr)->get_added_tick(entity);
            },
            // get_modified_tick
            [](const void* ptr, Entity entity) {
                return static_cast<const ComponentSparseSet<T>*>(ptr)->get_modified_tick(entity);
            },
        };
        return &f;
    }

   public:
    template <typename T>
    ComponentSparseSetInterface() {
        functor = get_functor<T>();
        data    = std::unique_ptr<void, void (*)(void*)>(
            new ComponentSparseSet<T>(), [](void* ptr) { delete static_cast<ComponentSparseSet<T>*>(ptr); });
    }
    template <typename T>
    ComponentSparseSetInterface(ComponentSparseSet<T> set) {
        functor = get_functor<T>();
        data    = std::unique_ptr<void, void (*)(void*)>(new ComponentSparseSet<T>(std::move(set)), [](void* ptr) {
            delete static_cast<ComponentSparseSet<T>*>(ptr);
        });
    }
    void clear() { functor->clear(data.get()); }
    size_t size() const { return functor->size(data.get()); }
    bool empty() const { return functor->empty(data.get()); }
    void check_change_ticks(Tick tick) { functor->check_change_ticks(data.get(), tick); }
    bool remove(Entity entity) { return functor->remove(data.get(), entity); }
    bool contains(Entity entity) const { return functor->contains(data.get(), entity); }
    std::optional<const void*> get(Entity entity) const { return functor->get(data.get(), entity); }
    std::optional<void*> get_mut(Entity entity) { return functor->get_mut(data.get(), entity); }
    std::optional<ComponentTicks> get_ticks(Entity entity) const { return functor->get_ticks(data.get(), entity); }
    std::optional<std::reference_wrapper<const Tick>> get_added_tick(Entity entity) const {
        return functor->get_added_tick(data.get(), entity);
    }
    std::optional<std::reference_wrapper<const Tick>> get_modified_tick(Entity entity) const {
        return functor->get_modified_tick(data.get(), entity);
    }

    const void* get_raw() const { return data.get(); }
    void* get_raw_mut() { return data.get(); }
};

template <typename I, typename V>
struct SparseSet {
   private:
    std::vector<V> _dense;      // actual data
    std::vector<I> _indices;    // from dense index to index
    SparseArray<I, I> _sparse;  // from index to dense index

   public:
    SparseSet() = default;

    void clear(this SparseSet& self) {
        self._dense.clear();
        self._indices.clear();
        self._sparse.clear();
    }
    size_t size(this const SparseSet& self) { return self._dense.size(); }
    bool empty(this const SparseSet& self) { return self.size() == 0; }

    template <typename... Args>
    void emplace(this SparseSet& self, I index, Args&&... args) {
        self._sparse.get(index)
            .and_then([&](std::reference_wrapper<const I> dense_index) -> std::optional<bool> {
                self._dense[dense_index.get()] = V(std::forward<Args>(args)...);
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
        return self._sparse.get(index).and_then([&](I dense_index) -> std::optional<std::reference_wrapper<const V>> {
            return std::cref(self._dense[dense_index]);
        });
    }
    std::optional<std::reference_wrapper<V>> get_mut(this SparseSet& self, I index) {
        return self._sparse.get(index).and_then([&](I dense_index) -> std::optional<std::reference_wrapper<V>> {
            return std::ref(self._dense[dense_index]);
        });
    }

    bool remove(this SparseSet& self, I index) {
        return self._sparse.remove(index)
            .and_then([&](I dense_index) -> std::optional<bool> {
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
    SparseSet<size_t, ComponentSparseSetInterface> sets;

   public:
    SparseSets(
        const std::shared_ptr<type_system::TypeRegistry>& registry = std::make_shared<type_system::TypeRegistry>())
        : registry(registry) {}

    size_t size(this const SparseSets& self) { return self.sets.size(); }
    bool empty(this const SparseSets& self) { return self.sets.empty(); }

    auto iter(this const SparseSets& self) { return self.sets.iter(); }
    auto iter_mut(this SparseSets& self) { return self.sets.iter_mut(); }

    std::optional<std::reference_wrapper<const ComponentSparseSetInterface>> get(this const SparseSets& self,
                                                                                 size_t type_id) {
        return self.sets.get(type_id);
    }
    std::optional<std::reference_wrapper<ComponentSparseSetInterface>> get_mut(this SparseSets& self, size_t type_id) {
        return self.sets.get_mut(type_id);
    }
    template <typename T>
    std::optional<std::reference_wrapper<const ComponentSparseSet<T>>> get_as(this SparseSets& self) {
        size_t type_id = self.registry->type_id<T>();
        return self.sets.get_mut(type_id).transform([](std::reference_wrapper<const ComponentSparseSetInterface> ptr) {
            return std::cref(*static_cast<const ComponentSparseSet<T>*>(ptr.get().get_raw()));
        });
    }
    template <typename T>
    std::optional<std::reference_wrapper<ComponentSparseSet<T>>> get_as_mut(this SparseSets& self) {
        size_t type_id = self.registry->type_id<T>();
        return self.sets.get_mut(type_id).transform([](std::reference_wrapper<ComponentSparseSetInterface> ptr) {
            return std::ref(*static_cast<ComponentSparseSet<T>*>(ptr.get().get_raw_mut()));
        });
    }
    void insert(this SparseSets& self, size_t type_id, ComponentSparseSetInterface set) {
        self.sets.emplace(type_id, std::move(set));
    }
    template <typename T>
    void insert(this SparseSets& self) {
        size_t type_id = self.registry->type_id<T>();
        self.sets.emplace(type_id, ComponentSparseSetInterface(ComponentSparseSet<T>()));
    }

    ComponentSparseSetInterface& get_or_insert(this SparseSets& self, size_t type_id, ComponentSparseSetInterface set) {
        return self.sets.get_mut(type_id)
            .or_else([&]() -> std::optional<std::reference_wrapper<ComponentSparseSetInterface>> {
                self.insert(type_id, std::move(set));
                return self.sets.get_mut(type_id);
            })
            .value()
            .get();
    }
    template <typename T>
    ComponentSparseSet<T>& get_or_insert(this SparseSets& self) {
        size_t type_id = self.registry->type_id<T>();
        return *static_cast<ComponentSparseSet<T>*>(
            self.get_or_insert(type_id, ComponentSparseSetInterface(ComponentSparseSet<T>())).get_raw_mut());
    }

    void clear(this SparseSets& self) { self.sets.clear(); }
    void check_change_ticks(this SparseSets& self, Tick tick) {
        for (auto&& [_, set] : self.sets.iter_mut()) {
            set.check_change_ticks(tick);
        }
    }
};
}  // namespace epix::core::storage