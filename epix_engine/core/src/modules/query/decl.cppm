module;

#include <concepts>
#include <functional>
#include <optional>

export module epix.core:query.decl;

import :archetype;
import :component;
import :world.decl;
import :tick;

namespace core {
/**
 * @brief For types that can be used as a element of query item, e.g. template argument of Item.
 */
template <typename T>
struct WorldQuery;
template <typename T>
struct QueryData;
template <typename T>
struct QueryFilter;

struct FilteredAccess;

template <typename Q>
concept world_query = requires(WorldQuery<Q> q) {
    typename WorldQuery<Q>::Fetch;
    typename WorldQuery<Q>::State;
    std::copyable<typename WorldQuery<Q>::Fetch>;
    std::movable<typename WorldQuery<Q>::State>;
    std::copyable<typename WorldQuery<Q>::State>;
    requires requires(const WorldQuery<Q>::State& state, WorldQuery<Q>::State& state_mut, WorldQuery<Q>::Fetch& fetch,
                      World& world, Tick tick, const Archetype& archetype, Table& table, const FilteredAccess& access,
                      FilteredAccess& access_mut, const Components& components,
                      const std::function<bool(TypeId)>& contains_component) {
        { WorldQuery<Q>::init_fetch(world, state, tick, tick) } -> std::same_as<typename WorldQuery<Q>::Fetch>;
        { WorldQuery<Q>::set_archetype(fetch, state, archetype, table) } -> std::same_as<void>;
        // { Q::set_table(fetch, state, table) } -> std::same_as<void>;
        {
            WorldQuery<Q>::set_access(state_mut, access)
        } -> std::same_as<void>;  // used for dynamic filtered fetch, not necessary
        { WorldQuery<Q>::update_access(state, access_mut) } -> std::same_as<void>;
        { WorldQuery<Q>::init_state(world) } -> std::same_as<typename WorldQuery<Q>::State>;
        { WorldQuery<Q>::get_state(components) } -> std::same_as<std::optional<typename WorldQuery<Q>::State>>;
        { WorldQuery<Q>::matches_component_set(state, contains_component) } -> std::same_as<bool>;
    };
};

template <typename T>
concept query_data = world_query<T> && requires(WorldQuery<T>::Fetch& fetch, Entity entity, TableRow row) {
    typename QueryData<T>::Item;  // the return type from fetch.
    typename QueryData<T>::ReadOnly;
    { QueryData<T>::readonly } -> std::convertible_to<bool>;
    { QueryData<T>::fetch(fetch, entity, row) } -> std::same_as<typename QueryData<T>::Item>;
    // State of its WorldQuery type should be convertible to its ReadOnly's WorldQuery State
    std::constructible_from<const typename WorldQuery<typename QueryData<T>::ReadOnly>::State&,
                            typename WorldQuery<T>::State>;
};

template <typename T>
concept query_filter = world_query<T> && requires(WorldQuery<T>::Fetch& fetch, Entity entity, TableRow row) {
    { QueryFilter<T>::archetypal } -> std::convertible_to<bool>;
    { QueryFilter<T>::filter_fetch(fetch, entity, row) } -> std::same_as<bool>;
};

export template <typename... Fs>
    requires((query_filter<Fs> && ...))
struct Filter;

template <query_data D, query_filter F = Filter<>>
struct QueryState;

export template <query_data D, query_filter F>
struct QueryIter;

export template <query_data D, query_filter F = Filter<>>
struct Query;

export template <typename... Ts>
struct With;
export template <typename... Ts>
struct Without;
export template <typename... Fs>
struct Or;
}  // namespace core