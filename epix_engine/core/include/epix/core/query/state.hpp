#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>

#include "../storage/bitvector.hpp"
#include "../world.hpp"
#include "access.hpp"
#include "fetch.hpp"
#include "filter.hpp"

namespace epix::core::query {
template <typename D, typename F = Filter<>>
    requires valid_query_data<QueryData<D>> && valid_query_filter<QueryFilter<F>>
struct QueryState {
   public:
    static QueryState create_uninit(World& world) {
        return QueryState(world.id(), WorldQuery<D>::init_state(world), WorldQuery<F>::init_state(world));
    }
    static std::optional<QueryState> create_from_const_uninit(const World& world) {
        auto fetch_state  = WorldQuery<D>::get_state(world.components());
        auto filter_state = WorldQuery<F>::get_state(world.components());
        if (fetch_state.has_value() && filter_state.has_value()) {
            return QueryState(world.id(), std::move(*fetch_state), std::move(*filter_state));
        } else {
            return std::nullopt;
        }
    }
    static QueryState create(World& world) {
        auto state = create_uninit(world);
        state.update_archetypes(world);
        return state;
    }
    static std::optional<QueryState> create_from_const(const World& world) {
        return create_from_const_uninit(world).transform([&world](auto&& state) {
            state.update_archetypes(world);
            return std::move(state);
        });
    }

    void update_archetypes(const World& world) {
        validate_world(world);
        if (_component_access.required().empty()) {
            std::span span = world.archetypes().archetypes;
            for (auto&& archetype : span.subspan(_archetype_version)) {
                new_archetype_internal(archetype);
            }
        } else {
            std::optional<std::pair<TypeId, size_t>> smallest;
            for (auto&& [id, potential] : world.archetypes().by_component) {
                size_t size = potential.size();
                smallest    = smallest
                               .and_then([&](const auto& current) -> std::optional<std::pair<TypeId, size_t>> {
                                   if (size < current.second) {
                                       return std::make_pair(id, size);
                                   } else {
                                       return current;
                                   }
                               })
                               .or_else([&]() -> std::optional<std::pair<TypeId, size_t>> {
                                   return std::make_pair(id, size);
                               });
            }
            if (smallest.has_value()) {
                auto [type_id, _] = *smallest;
                for (auto&& [id, _] : world.archetypes().by_component.at(type_id)) {
                    if (id.get() >= _archetype_version) {
                        new_archetype_internal(world.archetypes().get(id).value().get());
                    }
                }
            }
        }
        _archetype_version = world.archetypes().size();
    }

    template <typename NewD, typename NewF>
        requires valid_query_data<QueryData<NewD>> && valid_query_filter<QueryFilter<NewF>> &&
                 std::constructible_from<typename QueryData<NewD>::State, const typename QueryData<D>::State&> &&
                 std::constructible_from<typename QueryFilter<NewF>::State, const typename QueryFilter<F>::State&>
    QueryState<NewD, NewF> as_transmuted_state() const {
        return QueryState<NewD, NewF>(_world_id, typename WorldQuery<NewD>::State(_fetch_state),
                                      typename WorldQuery<NewF>::State(_filter_state));
    }
    QueryState<typename QueryData<D>::ReadOnly, F> as_readonly() const {
        return as_transmuted_state<typename QueryData<D>::ReadOnly, F>();
    }

    std::span<const ArchetypeId> matched_archetype_ids() const { return _matched_archetype_ids; }
    const WorldQuery<D>::State& fetch_state() const { return _fetch_state; }
    const WorldQuery<F>::State& filter_state() const { return _filter_state; }

   private:
    QueryState(WorldId world_id, WorldQuery<D>::State fetch_state, WorldQuery<F>::State filter_state)
        : _world_id(world_id),
          _archetype_version(0),
          _fetch_state(std::move(fetch_state)),
          _filter_state(std::move(filter_state)) {
        FilteredAccess access = FilteredAccess::matches_everything();
        WorldQuery<D>::update_access(_fetch_state, access);
        FilteredAccess filter_access = FilteredAccess::matches_everything();
        WorldQuery<F>::update_access(_filter_state, filter_access);
        access.merge(filter_access);
        _component_access = std::move(access);
    }

    void validate_world(const World& world) {
        if (world.id() != _world_id) {
            throw std::runtime_error("QueryState used with a different World than it was created for");
        }
    }
    bool new_archetype_internal(const Archetype& archetype) {
        bool data_match =
            WorldQuery<D>::matches_component_set(_fetch_state, [&](TypeId id) { return archetype.contains(id); });
        bool filter_match =
            WorldQuery<F>::matches_component_set(_filter_state, [&](TypeId id) { return archetype.contains(id); });
        bool access_match = matches_component_set([&](TypeId id) { return archetype.contains(id); });
        if (WorldQuery<D>::matches_component_set(_fetch_state, [&](TypeId id) { return archetype.contains(id); }) &&
            WorldQuery<F>::matches_component_set(_filter_state, [&](TypeId id) { return archetype.contains(id); }) &&
            matches_component_set([&](TypeId id) { return archetype.contains(id); })) {
            _matched_archetypes.set(archetype.id().get());
            _matched_archetype_ids.push_back(archetype.id());
            return true;
        }
        return false;
    }
    bool matches_component_set(const std::function<bool(TypeId)>& contains_component) const {
        return std::ranges::any_of(_component_access.filters(), [&](const AccessFilters& filter) {
            return std::ranges::all_of(filter.with.iter_ones(), contains_component) &&
                   std::ranges::all_of(filter.without.iter_ones(), [&](TypeId id) { return !contains_component(id); });
        });
    }

   private:
    WorldId _world_id;
    size_t _archetype_version;
    storage::bit_vector _matched_archetypes;
    FilteredAccess _component_access;
    std::vector<ArchetypeId> _matched_archetype_ids;
    WorldQuery<D>::State _fetch_state;
    WorldQuery<F>::State _filter_state;

    template <typename DD, typename FF>
        requires valid_query_data<QueryData<DD>> && valid_query_filter<QueryFilter<FF>>
    friend struct QueryIter;
};
}  // namespace epix::core::query