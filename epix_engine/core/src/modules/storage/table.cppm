module;

#include <algorithm>
#include <memory>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <vector>

export module epix.core:storage.table;

import :storage.sparse_set;
import :storage.dense;

namespace core {
struct Table {
   private:
    SparseSet<TypeId, Dense> _denses;
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
    void clear_entities(this Table& self) {
        self._entities.clear();
        for (auto&& [_, dense] : self._denses.iter_mut()) {
            dense.clear();
        }
    }
    /**
     * @brief Removes the components at the given dense index, and swap the last component into its place.
     *
     * @param dense_index The dense index of the component to remove.
     * @return std::optional<Entity> The entity that is swapped into the removed component's place.
     */
    std::optional<Entity> swap_remove(this Table& self, size_t dense_index);
    struct MoveReturn {
        size_t new_index;                      // index in the target table
        std::optional<Entity> swapped_entity;  // entity that was swapped in the source table, if any
    };
    MoveReturn move_to(this Table& self, size_t dense_index, Table& target);
    std::optional<std::pair<const void*, const void*>> get_data_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_data(); });
    }
    template <typename T>
    std::optional<std::span<const T>> get_data_as_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_data_as<T>(); });
    }
    std::optional<std::span<Tick>> get_added_ticks_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_added_ticks(); });
    }
    std::optional<std::span<Tick>> get_modified_ticks_for(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return dense.get_modified_ticks(); });
    }
    std::optional<const void*> get_data(this const Table& self, size_t type_id, uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get(index); });
    }
    std::optional<std::reference_wrapper<Tick>> get_added_tick(this const Table& self, size_t type_id, uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_added_tick(index); });
    }
    std::optional<std::reference_wrapper<Tick>> get_modified_tick(this const Table& self,
                                                                  size_t type_id,
                                                                  uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_modified_tick(index); });
    }
    std::optional<ComponentTicks> get_ticks(this const Table& self, size_t type_id, uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_ticks(index); });
    }
    std::optional<TickRefs> get_tick_refs(this const Table& self, size_t type_id, uint32_t index) {
        return self._denses.get(type_id).and_then([&](const Dense& dense) { return dense.get_tick_refs(index); });
    }
    void check_change_ticks(this Table& self, Tick tick) {
        for (auto&& [_, dense] : self._denses.iter_mut()) {
            dense.check_change_ticks(tick);
        }
    }

    std::optional<std::reference_wrapper<const Dense>> get_dense(this const Table& self, size_t type_id) {
        return self._denses.get(type_id).transform([](const Dense& dense) { return std::cref(dense); });
    }
    std::optional<std::reference_wrapper<Dense>> get_dense_mut(this Table& self, size_t type_id) {
        return self._denses.get_mut(type_id).transform([](Dense& dense) { return std::ref(dense); });
    }
    TableRow allocate(this Table& self, Entity entity) {
        size_t row = self._entities.size();
        self._entities.push_back(entity);
        for (auto&& [_, dense] : self._denses.iter_mut()) {
            dense.resize_uninitialized(row + 1);
        }
        return static_cast<uint32_t>(row);
    }
};

struct VecHash {
    size_t operator()(const std::vector<TypeId>& vec) const {
        size_t hash = 0;
        for (TypeId v : vec) {
            hash ^= std::hash<uint32_t>()(v) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

struct Tables {
   private:
    std::shared_ptr<TypeRegistry> _type_registry;
    std::unordered_map<std::vector<TypeId>, TableId, VecHash> _table_id_registry;
    std::vector<Table> _tables;

   public:
    explicit Tables(const std::shared_ptr<TypeRegistry>& registry) : _type_registry(registry) {
        // empty table is always at index 0
        _tables.emplace_back();
        _table_id_registry.insert({{}, 0});
    }
    size_t table_count(this const Tables& self) { return self._tables.size(); }
    bool empty(this const Tables& self) { return self._tables.empty(); }
    auto iter(this Tables& self) { return std::views::all(self._tables); }
    void clear(this Tables& self) { std::ranges::for_each(self._tables, &Table::clear_entities); }
    void check_change_ticks(this Tables& self, Tick tick) {
        for (auto& table : self._tables) {
            table.check_change_ticks(tick);
        }
    }
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
    std::optional<std::reference_wrapper<const Table>> get(this const Tables& self,
                                                           const std::vector<TypeId>& type_ids) {
        return self.get_id(type_ids).transform([&](size_t table_id) { return std::cref(self._tables[table_id]); });
    }
    std::optional<std::reference_wrapper<Table>> get_mut(this Tables& self, const std::vector<TypeId>& type_ids) {
        return self.get_id(type_ids).transform([&](size_t table_id) { return std::ref(self._tables[table_id]); });
    }
    Table& get_or_insert(this Tables& self, const std::vector<TypeId>& type_ids) {
        TableId table_id = self.get_id_or_insert(type_ids);
        return self._tables[table_id];
    }
    std::optional<TableId> get_id(this const Tables& self, const std::vector<TypeId>& type_ids) {
        return self._table_id_registry.contains(type_ids) ? std::optional(self._table_id_registry.at(type_ids))
                                                          : std::nullopt;
    }
    TableId get_id_or_insert(this Tables& self, const std::vector<TypeId>& type_ids);
};
}  // namespace core