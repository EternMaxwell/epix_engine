#pragma once

#include <algorithm>
#include <cstddef>

#include "../storage/bitvector.hpp"
#include "../world.hpp"
#include "access.hpp"
#include "fetch.hpp"
#include "filter.hpp"

namespace epix::core::query {
template <typename D, typename F>
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

    QueryIter<D, F> create_iter(World& world, Tick last_run, Tick this_run) const {
        validate_world(world);
        return QueryIter<D, F>(&world, this, last_run, this_run);
    }

    bool contains_archetype(ArchetypeId id) const { return _matched_archetypes.contains(id); }

    void update_archetypes(const World& world) {
        validate_world(world);
        if (_component_access.required().empty()) {
            std::span span = world.archetypes().archetypes;
            for (auto&& archetype : span.subspan(_archetype_version)) {
                new_archetype_internal(archetype);
            }
        } else {
            auto rng = _component_access.required().iter_ones() |
                       std::views::filter([&](TypeId id) { return world.archetypes().by_component.contains(id); }) |
                       std::views::transform([&](TypeId id) {
                           return std::make_pair(id, std::addressof(world.archetypes().by_component.at(id)));
                       });
            auto iter_min = std::ranges::min_element(rng, {}, [](auto&& pair) { return pair.second->size(); });
            if (iter_min != rng.end()) {
                auto&& [id, potential] = *iter_min;
                for (auto&& [id, _] : *potential) {
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
                 QueryData<NewD>::readonly /* &&
                 std::constructible_from<typename QueryData<NewD>::State, const typename QueryData<D>::State&> &&
                 std::constructible_from<typename QueryFilter<NewF>::State, const typename QueryFilter<F>::State&> */
                 && (sizeof(typename WorldQuery<NewD>::State) == sizeof(typename WorldQuery<D>::State)) &&
                 (sizeof(typename WorldQuery<NewF>::State) == sizeof(typename WorldQuery<F>::State))
    QueryState<NewD, NewF>& as_transmuted_state() const {
        // This is unsafe if the new query data or filter's state cannot be reinterpreted from the old one's state.
        return *reinterpret_cast<QueryState<NewD, NewF>*>(const_cast<QueryState<D, F>*>(this));
    }
    QueryState<typename QueryData<D>::ReadOnly, F>& as_readonly() const {
        return as_transmuted_state<typename QueryData<D>::ReadOnly, F>();
    }

    std::span<const ArchetypeId> matched_archetype_ids() const { return _matched_archetype_ids; }
    const WorldQuery<D>::State& fetch_state() const { return _fetch_state; }
    const WorldQuery<F>::State& filter_state() const { return _filter_state; }
    const FilteredAccess& component_access() const { return _component_access; }
    Query<D, F> query_with_ticks(World& world, Tick last_run, Tick this_run) {
        update_archetypes(world);
        return Query<D, F>(world, *this, last_run, this_run);
    }
    Query<D, F> query(World& world) { return query_with_ticks(world, world.last_change_tick(), world.change_tick()); }
    Query<D, F> query_manual_with_ticks(World& world, Tick last_run, Tick this_run) {
        return Query<D, F>(world, *this, last_run, this_run);
    }
    Query<D, F> query_manual(World& world) {
        return query_manual_with_ticks(world, world.last_change_tick(), world.change_tick());
    }
    QueryIter<D, F> iter_with_ticks(World& world, Tick last_run, Tick this_run) {
        update_archetypes(world);
        return create_iter(world, last_run, this_run);
    }
    QueryIter<D, F> iter(World& world) {
        update_archetypes(world);
        return create_iter(world, world.last_change_tick(), world.change_tick());
    }
    QueryIter<D, F> iter_manual_with_ticks(World& world, Tick last_run, Tick this_run) {
        return create_iter(world, last_run, this_run);
    }
    QueryIter<D, F> iter_manual(World& world) {
        return create_iter(world, world.last_change_tick(), world.change_tick());
    }
    typename AddOptional<typename QueryData<D>::Item>::type get_manual_with_ticks(World& world,
                                                                                  Entity entity,
                                                                                  Tick last_run,
                                                                                  Tick this_run) {
        update_archetypes(world);
        return world.entities().get(entity).and_then(
            [this, &world, entity, last_run, this_run](EntityLocation location) ->
            typename AddOptional<typename QueryData<D>::Item>::type {
                if (!contains_archetype(location.archetype_id)) return std::nullopt;
                auto& archetype = world.archetypes().get(location.archetype_id).value().get();
                auto fetch      = WorldQuery<D>::init_fetch(world, fetch_state(), last_run, this_run);
                auto filter     = WorldQuery<F>::init_fetch(world, filter_state(), last_run, this_run);
                auto& table     = world.storage_mut().tables.get_mut(archetype.table_id()).value().get();

                WorldQuery<D>::set_archetype(fetch, fetch_state(), archetype, table);
                WorldQuery<F>::set_archetype(filter, filter_state(), archetype, table);
                if (!QueryFilter<F>::filter_fetch(filter, entity, location.table_idx)) return std::nullopt;
                return QueryData<D>::fetch(fetch, entity, location.table_idx);
            });
    }
    typename AddOptional<typename QueryData<D>::Item>::type get_with_ticks(World& world,
                                                                           Entity entity,
                                                                           Tick last_run,
                                                                           Tick this_run) {
        update_archetypes(world);
        return get_manual_with_ticks(world, entity, last_run, this_run);
    }
    typename AddOptional<typename QueryData<D>::Item>::type get_manual(World& world, Entity entity) {
        return get_manual_with_ticks(world, entity, world.last_change_tick(), world.change_tick());
    }
    typename AddOptional<typename QueryData<D>::Item>::type get(World& world, Entity entity) {
        update_archetypes(world);
        return get_manual_with_ticks(world, entity, world.last_change_tick(), world.change_tick());
    }

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

    void validate_world(const World& world) const {
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
        return _component_access.filters().empty() ||
               std::ranges::any_of(_component_access.filters(), [&](const AccessFilters& filter) {
                   return std::ranges::all_of(filter.with.iter_ones(), contains_component) &&
                          std::ranges::all_of(filter.without.iter_ones(),
                                              [&](TypeId id) { return !contains_component(id); });
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

// implements for World::query and World::query_filtered
namespace epix::core {
template <typename D, typename F>
query::QueryState<D, F> World::query_filtered()
    requires(query::valid_query_data<query::QueryData<D>> && query::valid_query_filter<query::QueryFilter<F>>)
{
    return query::QueryState<D, F>::create(*this);
}
template <typename D>
query::QueryState<D, query::Filter<>> World::query()
    requires(query::valid_query_data<query::QueryData<D>>)
{
    return query::QueryState<D, query::Filter<>>::create(*this);
}
}  // namespace epix::core