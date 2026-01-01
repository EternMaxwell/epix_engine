#include <cassert>
#include <ranges>

#include "epix/core/archetype.hpp"

namespace epix::core {
namespace archetype {

Archetype Archetype::create(ComponentIndex& component_index,
                            ArchetypeId id,
                            TableId table_id,
                            std::vector<TypeId> table_components,
                            std::vector<TypeId> sparse_components) {
    Archetype arch;
    arch._archetype_id = id;
    arch._table_id     = table_id;
    auto table_size    = std::ranges::size(table_components);
    auto sparse_size   = std::ranges::size(sparse_components);
    arch._components.reserve(table_size + sparse_size);
#ifdef __cpp_lib_ranges_enumerate
    for (auto&& [idx, type_id] : table_components | std::views::enumerate) {
        arch._components.emplace(type_id, StorageType::Table);
        component_index[type_id][id] = ArchetypeRecord{static_cast<size_t>(idx)};
    }
#else
    for (size_t idx = 0; idx < table_size; ++idx) {
        TypeId type_id = table_components[idx];
        arch._components.emplace(type_id, StorageType::Table);
        component_index[type_id][id] = ArchetypeRecord{idx};
    }
#endif
    for (auto&& type_id : sparse_components) {
        arch._components.emplace(type_id, StorageType::SparseSet);
        component_index[type_id][id] = ArchetypeRecord{std::nullopt};
    }
    return arch;
}

ArchetypeSwapRemoveResult Archetype::swap_remove(ArchetypeRow arch_idx) {
    assert(arch_idx.get() < _entities.size());
    bool is_last                      = arch_idx.get() == _entities.size() - 1;
    Entity removed_entity             = _entities[arch_idx].entity;
    TableRow removed_entity_table_row = _entities[arch_idx].table_idx;
    std::swap(_entities[arch_idx], _entities.back());
    _entities.pop_back();
    if (!is_last) {
        return {std::optional(_entities[arch_idx].entity), removed_entity_table_row};
    } else {
        return {std::nullopt, removed_entity_table_row};
    }
}

std::pair<ArchetypeId, bool> Archetypes::get_id_or_insert(TableId table_id,
                                                          std::vector<TypeId> table_components,
                                                          std::vector<TypeId> sparse_components) {
    ArchetypeComponents archetype_components{
        .table_components  = std::move(table_components),
        .sparse_components = std::move(sparse_components),
    };
    if (auto it = by_components.find(archetype_components); it != by_components.end()) {
        return {it->second, false};
    } else {
        ArchetypeId new_id = static_cast<ArchetypeId>(archetypes.size());
        archetypes.emplace_back(Archetype::create(by_component, new_id, table_id, archetype_components.table_components,
                                                  archetype_components.sparse_components));
        by_components.insert({archetype_components, new_id});
        return {new_id, true};
    }
}

}  // namespace archetype
}  // namespace epix::core
