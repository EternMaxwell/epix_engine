#pragma once

#include <concepts>

#include "../archetype.hpp"
#include "../component.hpp"
#include "../fwd.hpp"
#include "access.hpp"

namespace epix::core::query {
/**
 * @brief For types that can be used as a element of query item, e.g. template argument of Item.
 */
template <typename T>
struct WorldQuery;
template <typename WQ>
struct QueryFilter;

template <typename T>
struct query_type;

struct FilteredAccess;

template <typename Q>
concept valid_world_query = requires(Q q) {
    typename Q::Fetch;
    typename Q::State;
    requires std::copyable<typename Q::Fetch>;
    requires std::movable<typename Q::State>;
    requires std::copyable<typename Q::State>;
    requires requires(const Q::State& state, Q::State& state_mut, Q::Fetch& fetch, World& world, Tick tick,
                      const archetype::Archetype& archetype, storage::Table& table, const FilteredAccess& access,
                      FilteredAccess& access_mut, const Components& components,
                      const std::function<bool(TypeId)>& contains_component) {
        { Q::init_fetch(world, state, tick, tick) } -> std::same_as<typename Q::Fetch>;
        { Q::set_archetype(fetch, state, archetype, table) } -> std::same_as<void>;
        // { Q::set_table(fetch, state, table) } -> std::same_as<void>;
        { Q::set_access(state_mut, access) } -> std::same_as<void>;  // used for dynamic filtered fetch, not necessary
        { Q::update_access(state, access_mut) } -> std::same_as<void>;
        { Q::init_state(world) } -> std::same_as<typename Q::State>;
        { Q::get_state(components) } -> std::same_as<std::optional<typename Q::State>>;
        { Q::matches_component_set(state, contains_component) } -> std::same_as<bool>;
    };
};

template <typename T>
concept valid_query_data =
    valid_world_query<WorldQuery<typename query_type<T>::type>> &&
    requires(WorldQuery<typename query_type<T>::type>::Fetch& fetch, Entity entity, TableRow row) {
        typename T::Item;  // the return type from fetch.
        typename T::ReadOnly;
        { T::readonly } -> std::convertible_to<bool>;
        { T::fetch(fetch, entity, row) } -> std::same_as<typename T::Item>;
        // State of its WorldQuery type should be convertible to its ReadOnly's WorldQuery State
        requires std::constructible_from<const typename WorldQuery<typename T::ReadOnly>::State&,
                                         typename WorldQuery<typename query_type<T>::type>::State>;
    };

template <typename T>
concept valid_query_filter =
    valid_world_query<WorldQuery<typename query_type<T>::type>> &&
    requires(WorldQuery<typename query_type<T>::type>::Fetch& fetch, Entity entity, TableRow row) {
        { T::archetypal } -> std::convertible_to<bool>;
        { T::filter_fetch(fetch, entity, row) } -> std::same_as<bool>;
    };

template <typename T>
    requires valid_world_query<WorldQuery<T>>
struct QueryData;
template <typename... Fs>
    requires((valid_query_filter<QueryFilter<Fs>> && ...))
struct Filter;

template <typename D, typename F = Filter<>>
    requires valid_query_data<QueryData<D>> && valid_query_filter<QueryFilter<F>>
struct QueryState;

template <typename D, typename F>
    requires valid_query_data<QueryData<D>> && valid_query_filter<QueryFilter<F>>
struct QueryIter;

template <typename D, typename F = Filter<>>
    requires(valid_query_data<QueryData<D>> && valid_query_filter<QueryFilter<F>>)
struct Query;
}  // namespace epix::core::query