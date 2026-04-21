module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <functional>
#include <optional>
#include <type_traits>
#endif
export module epix.core:query.decl;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :archetype;
import :component;
import :world.decl;
import :tick;

namespace epix::core {
/** @brief Trait class for types usable as query data elements (e.g. template arguments of Item).
 *  Specialize to define Fetch and State types for custom queries. */
export template <typename T>
struct WorldQuery {};
/** @brief Trait class defining how query data is fetched and its read-only variant.
 *  Specialize to define Item type, ReadOnly type, and fetch() method. */
export template <typename T>
struct QueryData {};
/** @brief Trait class defining query filter behavior.
 *  Specialize to define archetypal flag and filter_fetch() method. */
export template <typename T>
struct QueryFilter {};

/** @brief Forward declaration of the filtered access descriptor. */
export struct FilteredAccess;

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

/** @brief Concept for types that can appear as query data elements.
 *  Requires WorldQuery and QueryData specializations with Fetch, State,
 *  Item, ReadOnly, and fetch(). */
export template <typename T>
concept query_data = world_query<T> && requires(WorldQuery<T>::Fetch& fetch, Entity entity, TableRow row) {
    typename QueryData<T>::Item;  // the return type from fetch. most of the time should be T
    typename QueryData<T>::ReadOnly;
    typename std::bool_constant<QueryData<T>::readonly>;
    { QueryData<T>::fetch(fetch, entity, row) } -> std::same_as<typename QueryData<T>::Item>;
    // State of its WorldQuery type should be convertible to its ReadOnly's WorldQuery State
    requires std::constructible_from<const typename WorldQuery<typename QueryData<T>::ReadOnly>::State&,
                                     typename WorldQuery<T>::State>;
};
/** @brief Concept for read-only query data that does not require mutable access. */
export template <typename T>
concept readonly_query_data = query_data<T> && QueryData<T>::readonly;

/** @brief Concept for types usable as query filters.
 *  Requires archetypal flag and filter_fetch() method. */
export template <typename T>
concept query_filter = world_query<T> && requires(WorldQuery<T>::Fetch& fetch, Entity entity, TableRow row) {
    typename std::bool_constant<QueryFilter<T>::archetypal>;
    { QueryFilter<T>::filter_fetch(fetch, entity, row) } -> std::same_as<bool>;
};

/** @brief Composite query filter combining multiple sub-filters with AND logic.
 *  @tparam Fs Filter types, each satisfying query_filter. */
export template <typename... Fs>
    requires((query_filter<Fs> && ...))
struct Filter;

/** @brief Forward declaration of the query iterator. */
export template <query_data D, query_filter F>
struct QueryIter;

export template <query_data D, query_filter F = Filter<>>
struct Query;

template <typename T>
struct AddOptional {
    using type = std::optional<T>;
};
template <typename T>
    requires(std::is_reference_v<T>)
struct AddOptional<T> {
    using type = std::optional<std::reference_wrapper<std::remove_reference_t<T>>>;
};
}  // namespace core