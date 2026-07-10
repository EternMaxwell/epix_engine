#pragma once

#ifndef EPIX_CXX_MODULE
#include <cassert>
#include <cstddef>
#include <epix/common.hpp>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#endif

#include <epix/ecs/component.hpp>
#include <epix/ecs/query/access.hpp>
#include <epix/ecs/query/decl.hpp>
#include <epix/ecs/storage.hpp>
#include <epix/ecs/world/decl.hpp>

namespace epix::ecs {
/** @brief Represents the items (components) in a query result as a tuple.
 *  @tparam Ts World-query types whose data items form the tuple elements. */
EPIX_EXPORT template <internal::world_query... Ts>
struct Item : std::tuple<typename QueryData<Ts>::Item...> {
    using type = std::tuple<Ts...>;
    using base = std::tuple<typename QueryData<Ts>::Item...>;
    using base::base;
    /** @brief Construct from a const base tuple reference. */
    Item(const base& b) : base(b) {}
    /** @brief Construct from an rvalue base tuple. */
    Item(base&& b) : base(std::move(b)) {}
    /** @brief Dereference to get the underlying tuple. */
    std::tuple<typename QueryData<Ts>::Item...> operator*() { return *this; }
};

// Item itself is also a valid_world_query
template <internal::world_query... Ts>
struct WorldQuery<std::tuple<Ts...>> {
    using Fetch = std::tuple<typename WorldQuery<Ts>::Fetch...>;
    using State = std::tuple<typename WorldQuery<Ts>::State...>;
    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        return []<std::size_t... Is>(std::index_sequence<Is...>, World& world, const State& state, Tick last_run,
                                     Tick this_run) {
            return std::make_tuple(WorldQuery<Ts>::init_fetch(world, std::get<Is>(state), last_run, this_run)...);
        }(std::index_sequence_for<Ts...>{}, world, state, last_run, this_run);
    }
    static void set_archetype(Fetch& fetch, const State& state, const Archetype& archetype, internal::Table& table) {
        []<std::size_t... Is>(std::index_sequence<Is...>, Fetch& fetch, const State& state, const Archetype& archetype,
                              internal::Table& table) {
            (WorldQuery<Ts>::set_archetype(std::get<Is>(fetch), std::get<Is>(state), archetype, table), ...);
        }(std::index_sequence_for<Ts...>{}, fetch, state, archetype, table);
    }
    // static void set_table(Fetch& fetch, State& state, const internal::Table& table) {
    //     []<std::size_t... Is>(std::index_sequence<Is...>, Fetch& fetch, State& state, const internal::Table& table) {
    //         (WorldQuery<Ts>::set_table(std::get<Is>(fetch), std::get<Is>(state), table), ...);
    //     }(std::index_sequence_for<Ts...>{}, fetch, state, table);
    // }
    static void set_access(State& state, const FilteredAccess& access) {
        []<std::size_t... Is>(std::index_sequence<Is...>, State& state, const FilteredAccess& access) {
            (WorldQuery<Ts>::set_access(std::get<Is>(state), access), ...);
        }(std::index_sequence_for<Ts...>{}, state, access);
    }
    static void update_access(const State& state, FilteredAccess& access) {
        []<std::size_t... Is>(std::index_sequence<Is...>, const State& state, FilteredAccess& access) {
            (WorldQuery<Ts>::update_access(std::get<Is>(state), access), ...);
        }(std::index_sequence_for<Ts...>{}, state, access);
    }
    static State init_state(World& world) {
        return []<std::size_t... Is>(std::index_sequence<Is...>, World& world) {
            return std::make_tuple(WorldQuery<Ts>::init_state(world)...);
        }(std::index_sequence_for<Ts...>{}, world);
    }
    static std::optional<State> get_state(const Components& components) {
        return []<std::size_t... Is>(std::index_sequence<Is...>, const Components& components) -> std::optional<State> {
            std::tuple<std::optional<typename WorldQuery<Ts>::State>...> states{
                WorldQuery<Ts>::get_state(components)...};
            bool all_found = (true && ... && (std::get<Is>(states).has_value()));
            if (all_found) {
                return std::make_tuple(std::move(*std::get<Is>(states))...);
            } else {
                return std::nullopt;
            }
        }(std::index_sequence_for<Ts...>{}, components);
    }
    static bool matches_component_set(const State& state, internal::contains_component_fn auto&& contains_component) {
        return []<std::size_t... Is>(std::index_sequence<Is...>, const State& state,
                                     internal::contains_component_fn auto&& contains_component) {
            return true && (WorldQuery<Ts>::matches_component_set(std::get<Is>(state), contains_component) && ...);
        }(std::index_sequence_for<Ts...>{}, state, std::forward<decltype(contains_component)>(contains_component));
    }
};
template <internal::world_query... Ts>
struct WorldQuery<Item<Ts...>> : WorldQuery<std::tuple<Ts...>> {};

template <internal::world_query... Ts>
struct QueryData<std::tuple<Ts...>> {
    using Item                            = std::tuple<typename QueryData<Ts>::Item...>;
    using ReadOnly                        = std::tuple<typename QueryData<Ts>::ReadOnly...>;
    static inline constexpr bool readonly = (QueryData<Ts>::readonly && ...);
    static Item fetch(typename WorldQuery<std::tuple<Ts...>>::Fetch& fetch, Entity entity, TableRow row) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>, typename WorldQuery<std::tuple<Ts...>>::Fetch& fetch,
                                      Entity entity, TableRow row) {
            return Item(QueryData<Ts>::fetch(std::get<Is>(fetch), entity, row)...);
        }(std::index_sequence_for<Ts...>{}, fetch, entity, row);
    }
};
template <internal::world_query... Ts>
struct QueryData<Item<Ts...>> : QueryData<std::tuple<Ts...>> {};

/** @brief Type alias extracting the Item type from a query data descriptor.
 *  @tparam T Query data type satisfying query_data. */
EPIX_EXPORT template <query_data T>
using QueryItem = typename QueryData<T>::Item;

// implements for Entity
template <>
struct WorldQuery<Entity> {
    struct Fetch {};
    using State = std::tuple<>;
    static Fetch init_fetch(World&, const State&, Tick, Tick) noexcept { return Fetch{}; }
    static void set_archetype(Fetch&, const State&, const Archetype&, internal::Table&) noexcept {}
    // static void set_table(Fetch&, State&, const internal::Table&) {}
    static void set_access(State&, const FilteredAccess&) noexcept {}
    static void update_access(const State&, FilteredAccess&) noexcept {}
    static State init_state(World&) noexcept { return State{}; }
    static std::optional<State> get_state(const Components&) noexcept { return State{}; }
    static bool matches_component_set(const State&, internal::contains_component_fn auto&& contains_component) noexcept {
        return true;
    }
};
static_assert(internal::world_query<Entity>);
template <>
struct QueryData<Entity> {
    using Item                            = Entity;
    using ReadOnly                        = Entity;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<Entity>::Fetch&, Entity entity, TableRow) noexcept { return entity; }
};
static_assert(query_data<Entity>);

// implements for EntityLocation
template <>
struct WorldQuery<EntityLocation> {
    using Fetch = const Entities*;
    using State = std::tuple<>;
    static Fetch init_fetch(World& world, const State&, Tick, Tick) noexcept {
        return &internal::world_entities(world);
    }
    static void set_archetype(Fetch&, const State&, const Archetype&, internal::Table&) noexcept {}
    // static void set_table(Fetch&, State&, const internal::Table&) {}
    static void set_access(State&, const FilteredAccess&) noexcept {}
    static void update_access(const State&, FilteredAccess&) noexcept {}
    static State init_state(World&) noexcept { return State{}; }
    static std::optional<State> get_state(const Components&) noexcept { return State{}; }
    static bool matches_component_set(const State&, internal::contains_component_fn auto&& contains_component) noexcept {
        return true;
    }
};
static_assert(internal::world_query<EntityLocation>);
template <>
struct QueryData<EntityLocation> {
    using Item                            = EntityLocation;
    using ReadOnly                        = EntityLocation;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<EntityLocation>::Fetch& fetch, Entity entity, TableRow) {
        return fetch->get(entity).value();
    }
};
static_assert(query_data<EntityLocation>);

// implements for const Archetype&
template <>
struct WorldQuery<const Archetype&> {
    struct Fetch {
        const Entities* entities     = nullptr;
        const Archetypes* archetypes = nullptr;
    };
    using State = std::tuple<>;
    static Fetch init_fetch(World&, const State&, Tick, Tick) noexcept { return Fetch{}; }
    static void set_archetype(Fetch&, const State&, const Archetype&, internal::Table&) noexcept {}
    // static void set_table(Fetch&, State&, const internal::Table&) {}
    static void set_access(State&, const FilteredAccess&) noexcept {}
    static void update_access(const State&, FilteredAccess&) noexcept {}
    static State init_state(World&) noexcept { return State{}; }
    static std::optional<State> get_state(const Components&) noexcept { return State{}; }
    static bool matches_component_set(const State&, internal::contains_component_fn auto&& contains_component) noexcept {
        return true;
    }
};
static_assert(internal::world_query<const Archetype&>);
template <>
struct QueryData<const Archetype&> {
    using Item                            = const Archetype&;
    using ReadOnly                        = const Archetype&;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<const Archetype&>::Fetch& fetch, Entity entity, TableRow) {
        return fetch.archetypes->get(fetch.entities->get(entity).value().archetype_id).value().get();
    }
};
static_assert(query_data<const Archetype&>);

/** @brief Optional query fetch. Returns `std::optional<Ref<T>>` for `Opt<Ref<T>>`,
 *  `std::optional<Mut<T>>` for `Opt<Mut<T>>`, `std::optional<std::reference_wrapper<const T>>`
 *  for `Opt<const T&>`, `std::optional<std::reference_wrapper<T>>` for `Opt<T&>`.
 *  Matches even when the component is absent. */
EPIX_EXPORT template <internal::world_query T>
struct Opt {};  // empty definition needed for tuple.

template <internal::world_query T>
struct WorldQuery<Opt<T>> {
    struct Fetch {
        typename WorldQuery<T>::Fetch fetch;
        bool matches = false;
    };
    using State = typename WorldQuery<T>::State;
    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        return Fetch{.fetch = WorldQuery<T>::init_fetch(world, state, last_run, this_run), .matches = false};
    }
    static void set_archetype(Fetch& fetch, const State& state, const Archetype& archetype, internal::Table& table) {
        fetch.matches = WorldQuery<T>::matches_component_set(state, [&](TypeId id) { return archetype.contains(id); });
        if (fetch.matches) {
            WorldQuery<T>::set_archetype(fetch.fetch, state, archetype, table);
        }
    }
    // static void set_table(Fetch& fetch, State& state, const internal::Table& table) {
    //     if (fetch.matches) {
    //         WorldQuery<T>::set_table(fetch.fetch, state, table);
    //     }
    // }
    static void set_access(State& state, const FilteredAccess& access) { WorldQuery<T>::set_access(state, access); }
    static void update_access(const State& state, FilteredAccess& access) {
        // add_[read,write] for FilteredAccess also add them to the with, without set. But for optional fetch, we do not
        // want that. So a intermediate FilteredAccess is used.
        FilteredAccess state_access = access;
        WorldQuery<T>::update_access(state, state_access);
        access.access_mut().merge(state_access.access());
    }
    static State init_state(World& world) { return WorldQuery<T>::init_state(world); }
    static std::optional<State> get_state(const Components& components) { return WorldQuery<T>::get_state(components); }
    static bool matches_component_set(const State& state,
                                      internal::contains_component_fn auto&& contains_component) noexcept {
        return true;  // always true, because it is optional
    }
};

template <internal::world_query T>
struct QueryData<Opt<T>> {
    using Item                            = typename internal::AddOptional<typename QueryData<T>::Item>::type;
    using ReadOnly                        = Opt<typename QueryData<T>::ReadOnly>;
    static inline constexpr bool readonly = QueryData<T>::readonly;
    static Item fetch(WorldQuery<Opt<T>>::Fetch& fetch, Entity entity, TableRow row) {
        if (fetch.matches) {
            return QueryData<T>::fetch(fetch.fetch, entity, row);
        } else {
            return std::nullopt;
        }
    }
};

/** @brief Query item that yields true/false based on whether an entity has component T.
 *  Does not fetch any data, only checks existence.
 *  @tparam T Component type to check. */
EPIX_EXPORT template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct Has;

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Has<T>> {
    using Fetch = bool;
    using State = TypeId;
    static Fetch init_fetch(World&, const State&, Tick, Tick) noexcept { return false; }
    static void set_archetype(Fetch& fetch, const State& state, const Archetype& archetype, internal::Table&) noexcept {
        fetch = archetype.contains(state);
    }
    // static void set_table(Fetch& fetch, const State& state, const internal::Table& table) {
    //     fetch = table.has_dense(state);
    // }
    static void set_access(State&, const FilteredAccess&) noexcept {}
    static void update_access(const State& state, FilteredAccess& access) { access.access_mut().add_archetypal(state); }
    static State init_state(World& world) { return internal::world_type_registry(world).type_id<T>(); }
    static std::optional<State> get_state(const Components& components) { return components.registry().type_id<T>(); }
    static bool matches_component_set(const State& state,
                                      internal::contains_component_fn auto&& contains_component) noexcept {
        return true;  // always true, because it is just a marker
    }
};
static_assert(internal::world_query<Has<int>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<Has<T>> {
    using Item                            = bool;
    using ReadOnly                        = Has<T>;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<Has<T>>::Fetch& fetch, Entity, TableRow) noexcept { return fetch; }
};
static_assert(query_data<Has<int>>);
}  // namespace epix::ecs