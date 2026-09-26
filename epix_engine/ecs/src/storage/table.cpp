#include <spdlog/spdlog.h>

#include <cassert>
#include <epix/ecs/component/components.hpp>
#include <epix/ecs/storage/table.hpp>
#include <epix/meta.hpp>

namespace epix::ecs {
// These three are ordinary member functions rather than members with an explicit
// object parameter (this Table& self): MSVC rejects a DLL interface on an
// explicit object member (C2340), and the shared build has to export them.
std::optional<Entity> Table::swap_remove(size_t dense_index) {
    assert(dense_index < _entities.size());
    bool is_last = dense_index == _entities.size() - 1;
    for (auto&& [type_id, dense] : _denses.iter_mut()) {
        dense.swap_remove(dense_index);
    }
    std::swap(_entities[dense_index], _entities.back());
    _entities.pop_back();
    if (!is_last) {
        return _entities[dense_index];
    } else {
        return std::nullopt;
    }
}

Table::MoveReturn Table::move_to(size_t dense_index, Table& target) {
    assert(dense_index < _entities.size());
    spdlog::trace("[table] Moving entity at row {} to another table.", dense_index);
    size_t new_index = target._entities.size();
    target.allocate(_entities[dense_index]);
    // Iterate over source denses: if target has the type, move the value; otherwise
    // reserve an uninitialized slot in target for the incoming entity.
    for (auto&& [type_id, src_dense] : _denses.iter_mut()) {
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
    bool is_last = dense_index == _entities.size() - 1;
    std::swap(_entities[dense_index], _entities.back());
    _entities.pop_back();
    if (!is_last) {
        return {new_index, _entities[dense_index]};
    } else {
        return {new_index, std::nullopt};
    }
}

TableId Tables::get_id_or_insert(const std::vector<TypeId>& type_ids, const Components& components) {
    TableId table_id;
    if (auto it = _table_id_registry.find(type_ids); it != _table_id_registry.end()) {
        table_id = it->second;
    } else {
        table_id = _tables.size();
        spdlog::trace("[table] Creating new table id={} with {} column types.", static_cast<uint32_t>(table_id),
                      type_ids.size());
        _tables.emplace_back();
        Table& table = _tables.back();
        for (auto type_id : type_ids) {
            const meta::type_info& type_info = components.get_info(type_id).value().get().type_index().type_info();
            table._denses.emplace(type_id, Dense(type_info));
        }
        _table_id_registry.insert({type_ids, table_id});
    }
    return table_id;
}

}  // namespace epix::ecs
