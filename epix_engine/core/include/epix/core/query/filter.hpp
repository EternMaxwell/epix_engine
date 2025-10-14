#pragma once

#include <algorithm>

#include "../tick.hpp"
#include "../world.hpp"
#include "access.hpp"
#include "fwd.hpp"

namespace epix::core::query {
template <typename T>
struct query_type<QueryFilter<T>> {
    using type = T;
};

template <typename... Fs>
    requires((valid_query_filter<QueryFilter<Fs>> && ...))
struct WorldQuery<Filter<Fs...>> : WorldQuery<std::tuple<Fs...>> {};

template <typename... Fs>
    requires((valid_query_filter<QueryFilter<Fs>> && ...))
struct QueryFilter<Filter<Fs...>> {
    constexpr static inline bool archetypal = true && (Fs::archetypal && ...);
    static bool filter_fetch(WorldQuery<Filter<Fs...>>::Fetch& fetch, Entity entity, TableRow row) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return true && (QueryFilter<Fs>::filter_fetch(std::get<Is>(fetch), entity, row) && ...);
        }(std::index_sequence_for<Fs...>{});
    }
};

template <typename T>
struct With;
template <typename T>
struct Without;
template <typename... Fs>
struct Or;

// implements for With<T>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<With<T>> {
    struct Fetch {};
    using State = TypeId;
    static Fetch init_fetch(World&, const State&, Tick, Tick) { return Fetch{}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State& state, FilteredAccess& access) { access.add_with(state); }
    static State init_state(World& world) { return world.type_registry().type_id<T>(); }
    static std::optional<State> get_state(const Components& components) { return components.registry().type_id<T>(); }
    static bool matches_component_set(const State& state, const std::function<bool(TypeId)>& contains_component) {
        return contains_component(state);
    }
};
static_assert(valid_world_query<WorldQuery<With<int>>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryFilter<With<T>> {
    constexpr static inline bool archetypal = true;
    static bool filter_fetch(WorldQuery<With<T>>::Fetch&, Entity, TableRow) {
        // always true, because the filter is applied at archetype level. (e.g. access compatible)
        return true;
    }
};
static_assert(valid_query_filter<QueryFilter<With<int>>>);

// implements for Without<T>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Without<T>> {
    struct Fetch {};
    using State = TypeId;
    static Fetch init_fetch(World&, const State&, Tick, Tick) { return Fetch{}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State& state, FilteredAccess& access) { access.add_without(state); }
    static State init_state(World& world) { return world.type_registry().type_id<T>(); }
    static std::optional<State> get_state(const Components& components) { return components.registry().type_id<T>(); }
    static bool matches_component_set(const State& state, const std::function<bool(TypeId)>& contains_component) {
        return !contains_component(state);
    }
};
static_assert(valid_world_query<WorldQuery<Without<int>>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryFilter<Without<T>> {
    constexpr static inline bool archetypal = true;
    static bool filter_fetch(WorldQuery<Without<T>>::Fetch&, Entity, TableRow) {
        // always true, because the filter is applied at archetype level. (e.g. access compatible)
        return true;
    }
};
static_assert(valid_query_filter<QueryFilter<Without<int>>>);

// implements for Or<Fs...>
template <typename T>
struct OrFetch {
    WorldQuery<T>::Fetch fetch;
    bool matches = false;
};

template <typename... Fs>
    requires((valid_world_query<WorldQuery<Fs>> && ...))
struct WorldQuery<Or<Fs...>> {
    using Fetch = std::tuple<OrFetch<Fs>...>;
    using State = std::tuple<typename WorldQuery<Fs>::State...>;
    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(
                OrFetch<Fs>{.fetch   = WorldQuery<Fs>::init_fetch(world, std::get<Is>(state), last_run, this_run),
                            .matches = false}...);
        }(std::index_sequence_for<Fs...>{});
    }
    static void set_archetype(Fetch& fetch,
                              const State& state,
                              const archetype::Archetype& archetype,
                              storage::Table& table) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<size_t I>(std::integral_constant<size_t, I>) {
                    using F                    = std::tuple_element_t<I, std::tuple<Fs...>>;
                    std::get<I>(fetch).matches = WorldQuery<F>::matches_component_set(
                        std::get<I>(state), [&](TypeId id) { return archetype.contains(id); });
                    if (std::get<I>(fetch).matches) {
                        WorldQuery<F>::set_archetype(std::get<I>(fetch).fetch, std::get<I>(state), archetype, table);
                    }
                }(std::integral_constant<size_t, Is>{}),
                ...);
        }(std::index_sequence_for<Fs...>{});
    }
    // static void set_table(Fetch& fetch, State& state, const storage::Table& table) {
    //     [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    //         (
    //             [&]<size_t I>(std::integral_constant<size_t, I>) {
    //                 using F = std::tuple_element_t<I, std::tuple<Fs...>>;
    //                 std::get<I>(fetch).matches = WorldQuery<F>::matches_component_set(
    //                     std::get<I>(state), [&](TypeId id) { return table.has_dense(id); });
    //                 if (std::get<I>(fetch).matches) {
    //                     WorldQuery<F>::set_table(std::get<I>(fetch).fetch, std::get<I>(state), table);
    //                 }
    //             }(std::integral_constant<size_t, I>{}),
    //             ...);
    //     }(std::index_sequence_for<Fs...>{});
    // }
    static void set_access(State& state, const FilteredAccess& access) {}
    static void update_access(const State& state, FilteredAccess& access) {
        FilteredAccess new_access = FilteredAccess::matches_nothing();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<size_t I>(std::integral_constant<size_t, I>) {
                    using F                     = std::tuple_element_t<I, std::tuple<Fs...>>;
                    FilteredAccess intermediate = access;
                    WorldQuery<F>::update_access(std::get<I>(state), intermediate);
                    new_access.access_mut().merge(intermediate.access());
                    new_access.append_or(intermediate);
                }(std::integral_constant<size_t, Is>{}),
                ...);
        }(std::index_sequence_for<Fs...>{});
        new_access.required_mut() = std::move(access.required());
        access                    = std::move(new_access);
    }
    static State init_state(World& world) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(WorldQuery<Fs>::init_state(world)...);
        }(std::index_sequence_for<Fs...>{});
    }
    static std::optional<State> get_state(const Components& components) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> std::optional<State> {
            std::tuple<std::optional<typename WorldQuery<Fs>::State>...> states{
                WorldQuery<Fs>::get_state(components)...};
            bool all_found = (true && ... && (std::get<Is>(states).has_value()));
            if (all_found) {
                return std::make_tuple(std::move(*std::get<Is>(states))...);
            } else {
                return std::nullopt;
            }
        }(std::index_sequence_for<Fs...>{});
    }
    static bool matches_component_set(const State& state, const std::function<bool(TypeId)>& contains_component) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return false || (WorldQuery<Fs>::matches_component_set(std::get<Is>(state), contains_component) || ...);
        }(std::index_sequence_for<Fs...>{});
    }
};
static_assert(valid_world_query<WorldQuery<Or<With<int>, Without<float>>>>);

template <typename... Fs>
    requires((valid_query_filter<QueryFilter<Fs>> && ...))
struct QueryFilter<Or<Fs...>> {
    constexpr static inline bool archetypal = true && (QueryFilter<Fs>::archetypal && ...);
    static bool filter_fetch(WorldQuery<Or<Fs...>>::Fetch& fetch, Entity entity, TableRow row) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return false || ((std::get<Is>(fetch).matches &&
                              QueryFilter<Fs>::filter_fetch(std::get<Is>(fetch).fetch, entity, row)) ||
                             ...);
        }(std::index_sequence_for<Fs...>{});
    }
};
static_assert(valid_query_filter<QueryFilter<Or<With<int>, Without<float>>>>);

template <typename T>
struct Added;

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Added<T>> {
    struct Fetch {
        union {
            const storage::Dense* table_dense = nullptr;
            const storage::ComponentSparseSet* sparse_set;
        };
        TypeId component_id;
        bool is_sparse_set = false;
        Tick last_run;
        Tick this_run;
    };
    struct State {
        TypeId component_id;
        StorageType storage_type;
    };
    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        Fetch fetch{.table_dense   = nullptr,
                    .component_id  = state.component_id,
                    .is_sparse_set = state.storage_type == StorageType::SparseSet,
                    .last_run      = last_run,
                    .this_run      = this_run};
        if (fetch.is_sparse_set) {
            fetch.sparse_set = world.storage()
                                   .sparse_sets.get(state.component_id)
                                   .transform([](auto& ref) { return &ref.get(); })
                                   .value_or(nullptr);
        }
    }
    static void set_archetype(Fetch& fetch,
                              const State& state,
                              const archetype::Archetype& archetype,
                              storage::Table& table) {
        if (state.storage_type == StorageType::Table) {
            fetch.table_dense   = &table.get_dense(state.component_id).value().get();
            fetch.is_sparse_set = false;
        }
    }
    // static void set_table(Fetch& fetch, State& state, const storage::Table& table) {
    //     if (state.storage_type == StorageType::Table) {
    //         fetch.table_dense   = &table.get_dense(state.component_id).value().get();
    //         fetch.is_sparse_set = false;
    //     }
    // }
    static void set_access(State& state, const FilteredAccess& access) {}
    static void update_access(const State& state, FilteredAccess& access) {
        assert(access.access().has_component_write(state.component_id) &&
               "Added<T> conflicts with a previous access in this query. Shared access cannot coincide with exclusive "
               "access.");
        access.add_component_read(state.component_id);
    }
    static State init_state(World& world) {
        return State{.component_id = world.type_registry().type_id<T>(), .storage_type = storage_for<T>()};
    }
    static std::optional<State> get_state(const Components& components) {
        auto type_id = components.registry().type_id<T>();
        return components.get(type_id).transform([type_id](const ComponentInfo& info) {
            return State{.component_id = type_id, .storage_type = info.storage_type()};
        });
    }
    static bool matches_component_set(const State& state, const std::function<bool(TypeId)>& contains_component) {
        return contains_component(state.component_id);
    }
};
static_assert(valid_world_query<WorldQuery<Added<int>>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryFilter<Added<T>> {
    constexpr static inline bool archetypal = false;
    static bool filter_fetch(WorldQuery<Added<T>>::Fetch& fetch, Entity entity, TableRow row) {
        Tick added;
        if (fetch.is_sparse_set) {
            added = fetch.sparse_set->get_added_tick(entity).value().get();
        } else if (fetch.table_dense != nullptr) {
            added = fetch.table_dense->get_added_tick(row).value().get();
        } else {
            return false;
        }
        return added.newer_than(fetch.last_run, fetch.this_run);
    }
};
static_assert(valid_query_filter<QueryFilter<Added<int>>>);

template <typename T>
struct Modified;

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Modified<T>> : WorldQuery<Added<T>> {};
static_assert(valid_world_query<WorldQuery<Modified<int>>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryFilter<Modified<T>> {
    constexpr static inline bool archetypal = false;
    static bool filter_fetch(WorldQuery<Modified<T>>::Fetch& fetch, Entity entity, TableRow row) {
        Tick modified;
        if (fetch.is_sparse_set) {
            modified = fetch.sparse_set->get_modified_tick(entity).value().get();
        } else if (fetch.table_dense != nullptr) {
            modified = fetch.table_dense->get_modified_tick(row).value().get();
        } else {
            return false;
        }
        return modified.newer_than(fetch.last_run, fetch.this_run);
    }
};

template <typename T>
concept archetype_filter = valid_query_filter<T> && T::archetypal;
}  // namespace epix::core::query