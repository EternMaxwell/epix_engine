module;

#include <algorithm>
#include <cassert>
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

module epix.core;

import epix.meta;

import :storage.table;

namespace core {
std::optional<Entity> Table::swap_remove(this Table& self, size_t dense_index) {
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

Table::MoveReturn Table::move_to(this Table& self, size_t dense_index, Table& target) {
    assert(dense_index < self._entities.size());
    size_t new_index = target._entities.size();
    target.allocate(self._entities[dense_index]);
    // Iterate over source denses: if target has the type, move the value; otherwise
    // reserve an uninitialized slot in target for the incoming entity.
    for (auto&& [type_id, src_dense] : self._denses.iter_mut()) {
        // if target has this Dense, move the value into target
        target._denses.get_mut(type_id).and_then([&](Dense& target_dense) -> std::optional<bool> {
            src_dense.get_mut(dense_index).and_then([&](void* value) -> std::optional<bool> {
                target_dense.initialize_from_move(new_index, src_dense.get_ticks(dense_index).value(), value);
                return true;
            });
            return true;
        });
        src_dense.swap_remove(dense_index);
    }
    bool is_last = dense_index == self._entities.size() - 1;
    std::swap(self._entities[dense_index], self._entities.back());
    self._entities.pop_back();
    if (!is_last) {
        return {new_index, self._entities[dense_index]};
    } else {
        return {new_index, std::nullopt};
    }
}

TableId Tables::get_id_or_insert(this Tables& self, const std::vector<TypeId>& type_ids) {
    TableId table_id;
    if (auto it = self._table_id_registry.find(type_ids); it != self._table_id_registry.end()) {
        table_id = it->second;
    } else {
        table_id = self._tables.size();
        self._tables.emplace_back();
        Table& table = self._tables.back();
        for (size_t type_id : type_ids) {
            const meta::type_info& type_info = self._type_registry->type_index(type_id).type_info();
            table._denses.emplace(type_id, Dense(type_info));
        }
        self._table_id_registry.insert({type_ids, table_id});
    }
    return table_id;
}

}  // namespace core
