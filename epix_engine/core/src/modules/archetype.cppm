module;

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <vector>

export module epix.core:archetype;

import :entities;
import :type_registry;
import :component;

namespace core {
struct ArchetypeEntity {
    Entity entity;
    TableRow table_idx;
};
struct ArchetypeRecord {
    // index of the component in the archetype's table
    std::optional<std::size_t> table_dense;
};
struct ArchetypeComponents {
    std::vector<TypeId> table_components;
    std::vector<TypeId> sparse_components;
};
struct ArchetypeComponentsHash {
    std::size_t operator()(const ArchetypeComponents& ac) const {
        std::size_t hash = 0;
        for (auto type_id : ac.table_components) {
            hash ^= std::hash<std::uint32_t>()(type_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        for (auto type_id : ac.sparse_components) {
            hash ^= std::hash<std::uint32_t>()(type_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};
struct ArchetypeComponentsEqual {
    bool operator()(const ArchetypeComponents& a, const ArchetypeComponents& b) const {
        return a.table_components == b.table_components && a.sparse_components == b.sparse_components;
    }
};
using ComponentIndex =
    std::unordered_map<TypeId, std::unordered_map<ArchetypeId, ArchetypeRecord, std::hash<std::uint32_t>>>;

enum class ComponentStatus {
    Added,
    Exists,
};
template <typename V, typename T>
concept is_view_with_value_type = requires {
    requires std::ranges::view<V>;
    requires std::ranges::sized_range<V>;
    requires std::same_as<std::ranges::range_value_t<V>, T>;
};
struct ArchetypeSwapRemoveResult {
    std::optional<Entity> swapped_entity;  // entity that was swapped in, if any
    TableRow table_row;                    // table row of the removed entity
};
struct ArchetypeAfterBundleInsert {
   public:
    ArchetypeAfterBundleInsert(ArchetypeId archetype_id,
                               std::vector<TypeId> inserted,
                               std::size_t added_len,
                               std::vector<ComponentStatus> component_statuses,
                               std::vector<RequiredComponentConstructor> required_components)
        : archetype_id(archetype_id),
          _inserted(std::move(inserted)),
          _added_len(added_len),
          _component_statuses(std::move(component_statuses)),
          required_components(std::move(required_components)) {}

    std::span<const TypeId> inserted() const { return _inserted; }
    std::span<const TypeId> added() const { return inserted().subspan(0, _added_len); }
    std::span<const TypeId> existing() const { return inserted().subspan(_added_len); }

    auto iter_status() const { return std::views::all(_component_statuses); }

    ArchetypeId archetype_id;
    std::vector<RequiredComponentConstructor> required_components;

   private:
    std::vector<ComponentStatus> _component_statuses;  // status of each component in the bundle
    std::vector<TypeId> _inserted;  // components that declared by the bundle, including existing components
    std::size_t _added_len;         // Added components, including required components

    friend struct ArchetypeEdges;
};
struct ArchetypeEdges {
   public:
    std::optional<ArchetypeId> get_archetype_after_bundle_insert(BundleId bundle_id) const {
        return insert_bundle.get(bundle_id).transform(
            [](const ArchetypeAfterBundleInsert& abi) { return abi.archetype_id; });
    }
    std::optional<std::reference_wrapper<const ArchetypeAfterBundleInsert>> get_archetype_after_bundle_insert_detail(
        BundleId bundle_id) const {
        return insert_bundle.get(bundle_id).transform(
            [](const ArchetypeAfterBundleInsert& abi) { return std::cref(abi); });
    }
    std::optional<std::optional<ArchetypeId>> get_archetype_after_bundle_remove(BundleId bundle_id) const {
        return remove_bundle.get(bundle_id).transform([](const std::optional<ArchetypeId>& abi) { return abi; });
    }
    std::optional<std::optional<ArchetypeId>> get_archetype_after_bundle_take(BundleId bundle_id) const {
        return take_bundle.get(bundle_id).transform([](const std::optional<ArchetypeId>& abi) { return abi; });
    }

    void cache_archetype_after_bundle_insert(BundleId bundle_id,
                                             ArchetypeId archetype_id,
                                             std::vector<ComponentStatus> component_status,
                                             std::vector<RequiredComponentConstructor> required_components,
                                             std::vector<TypeId> added_components,
                                             std::vector<TypeId> existing_components) {
        std::size_t added_len = added_components.size();
        added_components.insert_range(added_components.end(), existing_components);
        insert_bundle.insert(bundle_id, archetype_id, std::move(added_components), added_len,
                             std::move(component_status), std::move(required_components));
    }
    void cache_archetype_after_bundle_remove(BundleId bundle_id, std::optional<ArchetypeId> archetype_id) {
        remove_bundle.insert(bundle_id, archetype_id);
    }
    void cache_archetype_after_bundle_take(BundleId bundle_id, std::optional<ArchetypeId> archetype_id) {
        take_bundle.insert(bundle_id, archetype_id);
    }

   private:
    SparseArray<BundleId, ArchetypeAfterBundleInsert> insert_bundle;
    SparseArray<BundleId, std::optional<ArchetypeId>> remove_bundle;
    SparseArray<BundleId, std::optional<ArchetypeId>> take_bundle;
};
struct Archetype {
   public:
    static Archetype create(ComponentIndex& component_index,
                            ArchetypeId id,
                            TableId table_id,
                            std::vector<TypeId> table_components,
                            std::vector<TypeId> sparse_components);
    static Archetype empty(ArchetypeId id) {
        Archetype arch;
        arch._archetype_id = id;
        arch._table_id     = 0;  // 0 is always the empty table
        return arch;
    }

    ArchetypeId id() const { return _archetype_id; }
    TableId table_id() const { return _table_id; }
    std::span<const ArchetypeEntity> entities() const { return _entities; }
    std::size_t size() const { return _entities.size(); }
    bool empty() const { return _entities.empty(); }
    const ArchetypeEdges& edges() const { return _edges; }
    ArchetypeEdges& edges_mut() { return _edges; }
    auto entities_with_location() const {
        return std::views::iota(0u, static_cast<unsigned>(_entities.size())) |
               std::views::transform([this](unsigned idx) -> std::pair<Entity, EntityLocation> {
                   const auto& ae = _entities[idx];
                   return std::pair<Entity, EntityLocation>{
                       ae.entity,
                       EntityLocation{_archetype_id, static_cast<std::uint32_t>(idx), _table_id, ae.table_idx}};
               });
    }
    struct IsTablePredicate {
        template <typename Pair>
        bool operator()(Pair const& pair) const noexcept {
            return std::get<1>(pair) == StorageType::Table;
        }
    };
    struct IsSparsePredicate {
        template <typename Pair>
        bool operator()(Pair const& pair) const noexcept {
            return std::get<1>(pair) == StorageType::SparseSet;
        }
    };

    auto table_components() const {
        return _components.iter() | std::views::filter(IsTablePredicate{}) | std::views::keys;
    }
    auto sparse_components() const {
        return _components.iter() | std::views::filter(IsSparsePredicate{}) | std::views::keys;
    }
    auto components() const { return _components.indices(); }
    std::size_t component_count() const { return _components.size(); }
    TableRow entity_table_row(ArchetypeRow arch_idx) const { return _entities[arch_idx].table_idx; }
    void set_entity_table_row(ArchetypeRow arch_idx, TableRow table_idx) { _entities[arch_idx].table_idx = table_idx; }
    EntityLocation allocate(Entity entity, TableRow table_idx) {
        ArchetypeRow arch_idx = static_cast<ArchetypeRow>(_entities.size());
        _entities.push_back({entity, table_idx});
        return {this->_archetype_id, arch_idx, this->_table_id, table_idx};
    }
    void reserve(std::size_t new_cap) { _entities.reserve(new_cap); }
    ArchetypeSwapRemoveResult swap_remove(ArchetypeRow arch_idx);
    bool contains(TypeId type_id) const { return _components.contains(type_id); }
    std::optional<StorageType> get_storage_type(TypeId type_id) const {
        return _components.get(type_id).transform(
            [](std::reference_wrapper<const StorageType> storage_type) { return storage_type.get(); });
    }
    void clear_entities() { _entities.clear(); }

   private:
    Archetype() = default;
    ArchetypeId _archetype_id;
    TableId _table_id;
    ArchetypeEdges _edges;
    std::vector<ArchetypeEntity> _entities;
    SparseSet<TypeId, StorageType> _components;
};
struct Archetypes {
    std::vector<Archetype> archetypes;
    std::unordered_map<ArchetypeComponents, ArchetypeId, ArchetypeComponentsHash, ArchetypeComponentsEqual>
        by_components;
    ComponentIndex by_component;

    Archetypes() {
        // default add an empty archetype at index 0
        archetypes.emplace_back(Archetype::empty(0));
        by_components.insert({ArchetypeComponents{{}, {}}, 0});
    }

    std::size_t size() const { return archetypes.size(); }

    const Archetype& get_empty() const { return archetypes[0]; }
    Archetype& get_empty_mut() { return archetypes[0]; }

    std::optional<std::reference_wrapper<const Archetype>> get(this const Archetypes& self, ArchetypeId id) {
        if (id.get() >= self.archetypes.size()) {
            return std::nullopt;
        }
        return std::cref(self.archetypes[id.get()]);
    }
    std::optional<std::reference_wrapper<Archetype>> get_mut(this Archetypes& self, ArchetypeId id) {
        if (id.get() >= self.archetypes.size()) {
            return std::nullopt;
        }
        return std::ref(self.archetypes[id.get()]);
    }
    auto iter() const { return std::views::all(archetypes); }
    auto iter_mut() { return std::views::all(archetypes); }

    std::pair<ArchetypeId, bool> get_id_or_insert(TableId table_id,
                                                  std::vector<TypeId> table_components,
                                                  std::vector<TypeId> sparse_components);
    void clear_entities() {
        for (auto& arch : archetypes) {
            arch.clear_entities();
        }
    }
};
}  // namespace core