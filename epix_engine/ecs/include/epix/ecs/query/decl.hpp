#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#include <functional>
#include <optional>
#include <type_traits>
#endif

#include <epix/ecs/archetype.hpp>
#include <epix/ecs/component.hpp>
#include <epix/ecs/tick.hpp>
#include <epix/ecs/world/decl.hpp>

namespace epix::ecs {
/** @brief Trait class for types usable as query data elements (e.g. template arguments of Item).
 *  Specialize to define Fetch and State types for custom queries. */
EPIX_EXPORT template <typename T>
struct WorldQuery {};
/** @brief Trait class defining how query data is fetched and its read-only variant.
 *  Specialize to define Item type, ReadOnly type, and fetch() method. */
EPIX_EXPORT template <typename T>
struct QueryData {};
/** @brief Trait class defining query filter behavior.
 *  Specialize to define archetypal flag and filter_fetch() method. */
EPIX_EXPORT template <typename T>
struct QueryFilter {};

/** @brief Forward declaration of the filtered access descriptor. */
EPIX_EXPORT struct FilteredAccess;

namespace internal {
template <typename Q>
concept world_query = requires(WorldQuery<Q> q) {
    typename WorldQuery<Q>::Fetch;
    typename WorldQuery<Q>::State;
    requires std::copyable<typename WorldQuery<Q>::Fetch>;
    requires std::movable<typename WorldQuery<Q>::State>;
    requires std::copyable<typename WorldQuery<Q>::State>;
    requires requires(const WorldQuery<Q>::State& state, WorldQuery<Q>::State& state_mut, WorldQuery<Q>::Fetch& fetch,
                      World& world, Tick tick, const Archetype& archetype, Table& table, const FilteredAccess& access,
                      FilteredAccess& access_mut, const Components& components,
                      utils::function_ref<bool(TypeId)> contains_component) {
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
concept contains_component_fn = std::invocable<T, TypeId> && std::same_as<bool, std::invoke_result_t<T, TypeId>>;
static_assert(contains_component_fn<utils::function_ref<bool(TypeId)>>);

template <typename T>
struct AddOptional {
    using type = std::optional<T>;
};
template <typename T>
    requires(std::is_reference_v<T>)
struct AddOptional<T> {
    using type = std::optional<std::reference_wrapper<std::remove_reference_t<T>>>;
};
}  // namespace internal

/** @brief Concept for types that can appear as query data elements.
 *  Requires WorldQuery and QueryData specializations with Fetch, State,
 *  Item, ReadOnly, and fetch(). */
EPIX_EXPORT template <typename T>
concept query_data = internal::world_query<T> && requires(WorldQuery<T>::Fetch& fetch, Entity entity, TableRow row) {
    typename QueryData<T>::Item;  // the return type from fetch. most of the time should be T
    typename QueryData<T>::ReadOnly;
    typename std::bool_constant<QueryData<T>::readonly>;
    { QueryData<T>::fetch(fetch, entity, row) } -> std::same_as<typename QueryData<T>::Item>;
    // State of its WorldQuery type should be convertible to its ReadOnly's WorldQuery State
    requires std::constructible_from<const typename WorldQuery<typename QueryData<T>::ReadOnly>::State&,
                                     typename WorldQuery<T>::State>;
};
/** @brief Concept for read-only query data that does not require mutable access. */
EPIX_EXPORT template <typename T>
concept readonly_query_data = query_data<T> && QueryData<T>::readonly;

/** @brief Concept for types usable as query filters.
 *  Requires archetypal flag and filter_fetch() method. */
EPIX_EXPORT template <typename T>
concept query_filter = internal::world_query<T> && requires(WorldQuery<T>::Fetch& fetch, Entity entity, TableRow row) {
    typename std::bool_constant<QueryFilter<T>::archetypal>;
    { QueryFilter<T>::filter_fetch(fetch, entity, row) } -> std::same_as<bool>;
};

/** @brief Composite query filter combining multiple sub-filters with AND logic.
 *  @tparam Fs Filter types, each satisfying query_filter. */
EPIX_EXPORT template <typename... Fs>
    requires((query_filter<Fs> && ...))
struct Filter;

/** @brief Forward declaration of the query iterator. */
EPIX_EXPORT template <query_data D, query_filter F>
struct QueryIter;

EPIX_EXPORT template <query_data D, query_filter F = Filter<>>
struct Query;
}  // namespace epix::ecs