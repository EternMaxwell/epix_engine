#pragma once

#include <cstddef>
#include <functional>
#include <ranges>
#include <type_traits>
#include <unordered_map>

#include "dense.hpp"
#include "fwd.hpp"
#include "sparse_set.hpp"

namespace epix::core::storage {
struct Table {
   private:
    SparseSet<size_t, Dense> _denses;
    std::vector<Entity> _entities;
    friend struct Tables;

   public:
    auto entities(this const Table& self) { return std::views::all(self._entities); }
    size_t capacity(this const Table& self) { return self._entities.capacity(); }
    size_t size(this const Table& self) { return self._entities.size(); }
    size_t type_count(this const Table& self) { return self._denses.size(); }
    bool empty(this const Table& self) { return self._entities.empty(); }
    void reserve(this Table& self, size_t additional) {
        self._entities.reserve(self._entities.size() + additional);
        for (auto&& [_, dense] : self._denses.iter_mut()) {
            dense.reserve(additional);
        }
    }
    bool has_dense(this const Table& self, size_t type_id) { return self._denses.contains(type_id); }
    /**
     * @brief Removes the components at the given dense index, and swap the last component into its place.
     *
     * @param dense_index The dense index of the component to remove.
     * @return std::optional<Entity> The entity that is swapped into the removed component's place.
     */
    std::optional<Entity> swap_remove(this Table& self, size_t dense_index) {
        assert(dense_index < self._entities.size());
        bool is_last = dense_index == self._entities.size() - 1;
        for (auto&& [type_id, dense] : self._denses.iter_mut()) {
            dense.swap_remove(dense_index);
        }
        std::swap(self._entities[dense_index], self._entities.back());
        self._entities.pop_back();
        if (!is_last) {
            return self._entities[dense_index];
        } else {
            return std::nullopt;
        }
    }
    struct MoveReturn {
        size_t new_index;                      // index in the target table
        std::optional<Entity> swapped_entity;  // entity that was swapped in the source table, if any
    };
    MoveReturn move_to(this Table& self, size_t dense_index, Table& target) {
        size_t new_index = target._entities.size();
        for (auto&& [type_id, dense] : self._denses.iter_mut()) {
            target._denses.get_mut(type_id).and_then([&](Dense& target_dense) -> std::optional<bool> {
                dense.get_mut(dense_index).and_then([&](void* value) -> std::optional<bool> {
                    target_dense.push_move({0, 0}, value);
                    return true;
                });
                return true;
            });
            dense.swap_remove(dense_index);
        }
        bool is_last = dense_index == self._entities.size() - 1;
        std::swap(self._entities[dense_index], self._entities.back());
        target._entities.push_back(self._entities.back());
        self._entities.pop_back();
        if (!is_last) {
            return {new_index, self._entities[dense_index]};
        } else {
            return {new_index, std::nullopt};
        }
    }
    std::optional<std::pair<const void*, const void*>> get_data_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_data(); });
    }
    template <typename T>
    std::optional<std::span<const T>> get_data_as_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_data_as<T>(); });
    }
    std::optional<std::span<const Tick>> get_added_ticks_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_added_ticks(); });
    }
    std::optional<std::span<const Tick>> get_modified_ticks_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_modified_ticks(); });
    }
    std::optional<const void*> get_data(this const Table& self, size_t type_id, uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get(index); });
    }
    std::optional<std::reference_wrapper<const Tick>> get_added_tick(this const Table& self,
                                                                     size_t type_id,
                                                                     uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_added_tick(index); });
    }
    std::optional<std::reference_wrapper<const Tick>> get_modified_tick(this const Table& self,
                                                                        size_t type_id,
                                                                        uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_modified_tick(index); });
    }
    std::optional<ComponentTicks> get_ticks(this const Table& self, size_t type_id, uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_ticks(index); });
    }

    std::optional<std::reference_wrapper<const Dense>> get_dense(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return std::cref(dense); });
    }
    std::optional<std::reference_wrapper<Dense>> get_dense_mut(this Table& self, size_t type_id) {
        return self._denses.get_mut(type_id).transform([](Dense& dense) { return std::ref(dense); });
    }
};
struct Tables {
    struct VecHash {
        size_t operator()(const std::vector<size_t>& vec) const {
            size_t hash = 0;
            for (size_t v : vec) {
                hash ^= std::hash<size_t>()(v) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

   private:
    std::shared_ptr<type_system::TypeRegistry> _type_registry;
    std::unordered_map<std::vector<size_t>, size_t, VecHash> _table_id_registry;
    std::vector<Table> _tables;

   public:
    size_t table_count(this const Tables& self) { return self._tables.size(); }
    bool empty(this const Tables& self) { return self._tables.empty(); }
    std::optional<std::reference_wrapper<const Table>> get(this const Tables& self, size_t table_id) {
        if (table_id >= self._tables.size()) {
            return std::nullopt;
        }
        return std::cref(self._tables[table_id]);
    }
    std::optional<std::reference_wrapper<Table>> get_mut(this Tables& self, size_t table_id) {
        if (table_id >= self._tables.size()) {
            return std::nullopt;
        }
        return std::ref(self._tables[table_id]);
    }
    std::optional<size_t> get_table_id(this const Tables& self, const std::vector<size_t>& type_ids) {
        return self._table_id_registry.find(type_ids) != self._table_id_registry.end()
                   ? std::optional(self._table_id_registry.at(type_ids))
                   : std::nullopt;
    }
    template <typename... Components>
    std::optional<size_t> get_table_id(this const Tables& self) {
        std::vector<size_t> type_ids = {self._type_registry->type_id<Components>()...};
        return self.get_table_id(type_ids);
    }
    template <typename... Components>
    size_t get_table_id_or_insert(this Tables& self) {
        std::vector<size_t> type_ids = {self._type_registry->type_id<Components>()...};
        size_t table_id              = self.get_table_id(type_ids).value_or([&]() {
            Table table;
            for (size_t type_id : type_ids) {
                table._denses.emplace(type_id, Dense(self._type_registry->type_info(type_id)));
            }
            self._tables.emplace_back(std::move(table));
            return self._tables.size() - 1;
        }());
        return table_id;
    }
};
}  // namespace epix::core::storage