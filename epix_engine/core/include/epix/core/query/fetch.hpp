#pragma once

#include <vcruntime_typeinfo.h>

#include <concepts>
#include <cstddef>
#include <functional>

#include "../archetype.hpp"
#include "../entities.hpp"
#include "../storage/table.hpp"
#include "../world/entity_ref.hpp"
#include "../world_cell.hpp"
#include "access.hpp"
#include "epix/core/archetype.hpp"
#include "epix/core/fwd.hpp"

namespace epix::core::query {
/**
 * @brief For types that can be used as a element of query item, e.g. template argument of Item.
 */
template <typename T>
struct WorldQuery;

template <typename Q>
concept valid_world_query = requires(Q q) {
    typename Q::Fetch;
    typename Q::State;
    requires requires(const Q::State& state, Q::State& state_mut, Q::Fetch& fetch, WorldCell& world, Tick tick,
                      const archetype::Archetype& archetype, const storage::Table& table, const FilteredAccess& access,
                      FilteredAccess& access_mut, const Components& components) {
        { Q::init_fetch(world, state, tick, tick) } -> std::same_as<typename Q::Fetch>;
        { Q::set_archetype(fetch, state, archetype, table) } -> std::same_as<void>;
        // { Q::set_table(fetch, state, table) } -> std::same_as<void>;
        { Q::set_access(state_mut, access) } -> std::same_as<void>;  // used for dynamic filtered fetch, not necessary
        { Q::update_access(state, access_mut) } -> std::same_as<void>;
        { Q::init_state(world) } -> std::same_as<typename Q::State>;
        { Q::get_state(components) } -> std::same_as<std::optional<typename Q::State>>;
        { Q::matches_archetype(state, archetype) } -> std::same_as<bool>;
    };
};
/**
 * @brief Represents the items(components) in a query result.
 */
template <typename... Ts>
    requires(valid_world_query<WorldQuery<Ts>> && ...)
struct Item;

// Item itself is also a valid_world_query
template <typename... Ts>
    requires(valid_world_query<WorldQuery<Ts>> && ...)
struct WorldQuery<Item<Ts...>> {
    using Fetch = std::tuple<typename WorldQuery<Ts>::Fetch...>;
    using State = std::tuple<typename WorldQuery<Ts>::State...>;
    static Fetch init_fetch(WorldCell& world, const State& state, Tick last_run, Tick this_run) {
        return []<size_t... Is>(std::index_sequence<Is...>, WorldCell& world, const State& state, Tick last_run,
                                Tick this_run) {
            return std::make_tuple(WorldQuery<Ts>::init_fetch(world, std::get<Is>(state), last_run, this_run)...);
        }(std::index_sequence_for<Ts...>{}, world, state, last_run, this_run);
    }
    static void set_archetype(Fetch& fetch,
                              const State& state,
                              const archetype::Archetype& archetype,
                              const storage::Table& table) {
        []<size_t... Is>(std::index_sequence<Is...>, Fetch& fetch, const State& state,
                         const archetype::Archetype& archetype, const storage::Table& table) {
            (WorldQuery<Ts>::set_archetype(std::get<Is>(fetch), std::get<Is>(state), archetype, table), ...);
        }(std::index_sequence_for<Ts...>{}, fetch, state, archetype, table);
    }
    // static void set_table(Fetch& fetch, State& state, const storage::Table& table) {
    //     []<size_t... Is>(std::index_sequence<Is...>, Fetch& fetch, State& state, const storage::Table& table) {
    //         (WorldQuery<Ts>::set_table(std::get<Is>(fetch), std::get<Is>(state), table), ...);
    //     }(std::index_sequence_for<Ts...>{}, fetch, state, table);
    // }
    static void set_access(State& state, const FilteredAccess& access) {
        []<size_t... Is>(std::index_sequence<Is...>, State& state, const FilteredAccess& access) {
            (WorldQuery<Ts>::set_access(std::get<Is>(state), access), ...);
        }(std::index_sequence_for<Ts...>{}, state, access);
    }
    static void update_access(const State& state, FilteredAccess& access) {
        []<size_t... Is>(std::index_sequence<Is...>, const State& state, FilteredAccess& access) {
            (WorldQuery<Ts>::update_access(std::get<Is>(state), access), ...);
        }(std::index_sequence_for<Ts...>{}, state, access);
    }
    static State init_state(WorldCell& world) {
        return []<size_t... Is>(std::index_sequence<Is...>, WorldCell& world) {
            return std::make_tuple(WorldQuery<Ts>::init_state(world)...);
        }(std::index_sequence_for<Ts...>{}, world);
    }
    static std::optional<State> get_state(const Components& components) {
        return []<size_t... Is>(std::index_sequence<Is...>, const Components& components) -> std::optional<State> {
            std::optional<std::tuple<typename WorldQuery<Ts>::State...>> result;
            bool all_found = (true && ... && (WorldQuery<Ts>::get_state(components).has_value()));
            if (all_found) {
                result = std::make_tuple(*(WorldQuery<Ts>::get_state(components))...);
            }
            return result;
        }(std::index_sequence_for<Ts...>{}, components);
    }
    static bool matches_archetype(const State& state, const archetype::Archetype& archetype) {
        return []<size_t... Is>(std::index_sequence<Is...>, const State& state, const archetype::Archetype& archetype) {
            return true && (WorldQuery<Ts>::matches_archetype(std::get<Is>(state), archetype) && ...);
        }(std::index_sequence_for<Ts...>{}, state, archetype);
    }
};

template <typename T>
    requires valid_world_query<WorldQuery<T>>
struct QueryData;

template <typename T>
struct query_type;
template <typename T>
struct query_type<QueryData<T>> {
    using type = T;
};

template <typename T>
concept valid_query_data =
    valid_world_query<WorldQuery<typename query_type<T>::type>> &&
    requires(WorldQuery<typename query_type<T>::type>::Fetch& fetch, Entity entity, TableRow row) {
        typename T::Item;  // the return type from fetch.
        typename T::ReadOnly;
        { T::readonly } -> std::convertible_to<bool>;
        { T::fetch(fetch, entity, row) } -> std::same_as<typename T::Item>;
    };

template <typename T>
using QueryItem = typename QueryData<T>::Item;

// implements for Entity
template <>
struct WorldQuery<Entity> {
    struct Fetch {};
    struct State {};
    static Fetch init_fetch(WorldCell&, const State&, Tick, Tick) { return Fetch{}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, const storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State&, FilteredAccess&) {}
    static State init_state(WorldCell&) { return State{}; }
    static std::optional<State> get_state(const Components&) { return State{}; }
    static bool matches_archetype(const State&, const archetype::Archetype&) { return true; }
};
static_assert(valid_world_query<WorldQuery<Entity>>);
template <>
struct QueryData<Entity> {
    using Item                            = Entity;
    using ReadOnly                        = Entity;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<Entity>::Fetch&, Entity entity, TableRow) { return entity; }
};
static_assert(valid_query_data<QueryData<Entity>>);

// implements for EntityLocation
template <>
struct WorldQuery<EntityLocation> {
    struct Fetch {
        const Entities* entities = nullptr;
    };
    struct State {};
    static Fetch init_fetch(WorldCell& world, const State&, Tick, Tick) { return Fetch{&world.entities()}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, const storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State&, FilteredAccess&) {}
    static State init_state(WorldCell&) { return State{}; }
    static std::optional<State> get_state(const Components&) { return State{}; }
    static bool matches_archetype(const State&, const archetype::Archetype&) { return true; }
};
static_assert(valid_world_query<WorldQuery<EntityLocation>>);
template <>
struct QueryData<EntityLocation> {
    using Item                            = EntityLocation;
    using ReadOnly                        = EntityLocation;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<EntityLocation>::Fetch& fetch, Entity entity, TableRow) {
        return fetch.entities->get(entity).value();
    }
};
static_assert(valid_query_data<QueryData<EntityLocation>>);

// implements for EntityRef
template <>
struct WorldQuery<EntityRef> {
    struct Fetch {
        WorldCell* world = nullptr;
    };
    struct State {};
    static Fetch init_fetch(WorldCell& world, const State&, Tick, Tick) { return Fetch{&world}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, const storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State&, FilteredAccess& access) {
        assert(!access.access().has_any_component_write() &&
               "EntityRef conflicts with a previous access in this query. Shared access cannot coincide with exclusive "
               "access.");
        access.access_mut().read_all_components();
    }
    static State init_state(WorldCell&) { return State{}; }
    static std::optional<State> get_state(const Components&) { return State{}; }
    static bool matches_archetype(const State&, const archetype::Archetype&) { return true; }
};
static_assert(valid_world_query<WorldQuery<EntityRef>>);
template <>
struct QueryData<EntityRef> {
    using Item                            = EntityRef;
    using ReadOnly                        = EntityRef;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<EntityRef>::Fetch& fetch, Entity entity, TableRow) {
        return EntityRef(entity, fetch.world);
    }
};
static_assert(valid_query_data<QueryData<EntityRef>>);

// implements for EntityRefMut
template <>
struct WorldQuery<EntityRefMut> {
    struct Fetch {
        WorldCell* world = nullptr;
    };
    struct State {};
    static Fetch init_fetch(WorldCell& world, const State&, Tick, Tick) { return Fetch{&world}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, const storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State&, FilteredAccess& access) {
        assert(!access.access().has_any_component_read() &&
               "EntityRefMut conflicts with a previous access in this query. Exclusive access cannot coincide with "
               "shared access.");
        access.access_mut().write_all_components();
    }
    static State init_state(WorldCell&) { return State{}; }
    static std::optional<State> get_state(const Components&) { return State{}; }
    static bool matches_archetype(const State&, const archetype::Archetype&) { return true; }
};
static_assert(valid_world_query<WorldQuery<EntityRefMut>>);
template <>
struct QueryData<EntityRefMut> {
    using Item                            = EntityRefMut;
    using ReadOnly                        = EntityRef;
    static inline constexpr bool readonly = false;
    static Item fetch(WorldQuery<EntityRefMut>::Fetch& fetch, Entity entity, TableRow) {
        return EntityRefMut(entity, fetch.world);
    }
};
static_assert(valid_query_data<QueryData<EntityRefMut>>);

// implements for const Archetype&
template <>
struct WorldQuery<const archetype::Archetype&> {
    struct Fetch {
        const Entities* entities                = nullptr;
        const archetype::Archetypes* archetypes = nullptr;
    };
    struct State {};
    static Fetch init_fetch(WorldCell&, const State&, Tick, Tick) { return Fetch{}; }
    static void set_archetype(Fetch&, const State&, const archetype::Archetype&, const storage::Table&) {}
    // static void set_table(Fetch&, State&, const storage::Table&) {}
    static void set_access(State&, const FilteredAccess&) {}
    static void update_access(const State&, FilteredAccess&) {}
    static State init_state(WorldCell&) { return State{}; }
    static std::optional<State> get_state(const Components&) { return State{}; }
    static bool matches_archetype(const State&, const archetype::Archetype&) { return true; }
};
static_assert(valid_world_query<WorldQuery<const archetype::Archetype&>>);
template <>
struct QueryData<const archetype::Archetype&> {
    using Item                            = const archetype::Archetype&;
    using ReadOnly                        = const archetype::Archetype&;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<const archetype::Archetype&>::Fetch& fetch, Entity entity, TableRow) {
        return fetch.archetypes->get(fetch.entities->get(entity).value().archetype_id).value().get();
    }
};
static_assert(valid_query_data<QueryData<const archetype::Archetype&>>);

// implements for Ref<T>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Ref<T>> {
    struct Fetch {
        union {
            const storage::Dense* table_dense = nullptr;
            const storage::ComponentSparseSet* sparse_set;
        };
        TypeId component_id;
        bool is_sparse_set = false;
    };
    struct State {
        TypeId component_id;
        StorageType storage_type;
    };
    static Fetch init_fetch(WorldCell& world, const State& state, Tick, Tick) {
        auto result = Fetch{.table_dense   = nullptr,
                            .component_id  = state.component_id,
                            .is_sparse_set = state.storage_type == StorageType::SparseSet};
        if (result.is_sparse_set) {
            result.sparse_set = world.storage().sparse_sets.get(state.component_id).value_or(nullptr);
        }
        return result;
    }
    static void set_archetype(Fetch& fetch,
                              const State& state,
                              const archetype::Archetype& archetype,
                              const storage::Table& table) {
        if (state.storage_type == StorageType::Table) {
            fetch.table_dense   = &table.get_dense(state.component_id).value().get();
            fetch.is_sparse_set = false;
        }
    }
    // static void set_table(Fetch& fetch, State& state, const storage::Table& table) {
    //     if (state.storage_type == StorageType::Table) {
    //         fetch.table_dense = &table.get_dense(state.component_id).value().get();
    //     }
    // }
    static void set_access(State& state, const FilteredAccess& access) {}
    static void update_access(const State& state, FilteredAccess& access) {
        access.add_component_read(state.component_id);
    }
    static State init_state(WorldCell& world) {
        return State{.component_id = TypeId(typeid(T)), .storage_type = storage_for<T>()};
    }
    static std::optional<State> get_state(const Components& components) {
        auto type_id = components.registry().type_id<T>();
        return components.get(type_id).transform([type_id](const ComponentInfo& info) {
            return State{.component_id = type_id, .storage_type = info.storage_type()};
        });
    }
    static bool matches_archetype(const State& state, const archetype::Archetype& archetype) {
        return archetype.contains(state.component_id);
    }
};
static_assert(valid_world_query<WorldQuery<Ref<int>>>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<Ref<T>> {
    using Item                            = Ref<T>;
    using ReadOnly                        = Ref<T>;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<Ref<T>>::Fetch& fetch, Entity entity, TableRow row) {
        if (fetch.is_sparse_set) {
            return fetch.sparse_set->template get_as<T>(entity)
                .transform([&](const T& value) {
                    return Ref<T>(&value, Ticks::from_refs(fetch.sparse_set->get_tick_refs(entity).value(), 0, 0));
                })
                .value();
        } else {
            return fetch.table_dense->template get_as<T>(row)
                .transform([&](const T& value) {
                    return Ref<T>(&value, Ticks::from_refs(fetch.table_dense->get_tick_refs(row).value(), 0, 0));
                })
                .value();
        }
    }
};
static_assert(valid_query_data<QueryData<Ref<int>>>);

// implements for Mut<T>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Mut<T>> {
    struct Fetch {
        union {
            storage::Dense* table_dense = nullptr;
            storage::ComponentSparseSet* sparse_set;
        };
        TypeId component_id;
        bool is_sparse_set = false;
    };
    struct State {
        TypeId component_id;
        StorageType storage_type;
    };
    static Fetch init_fetch(WorldCell& world, const State& state, Tick, Tick) {
        auto result = Fetch{.table_dense   = nullptr,
                            .component_id  = state.component_id,
                            .is_sparse_set = state.storage_type == StorageType::SparseSet};
        if (result.is_sparse_set) {
            result.sparse_set = world.storage_mut().sparse_sets.get_mut(state.component_id).value().get();
        }
        return result;
    }
    static void set_archetype(Fetch& fetch,
                              const State& state,
                              const archetype::Archetype& archetype,
                              const storage::Table& table) {
        if (state.storage_type == StorageType::Table) {
            fetch.table_dense   = &table.get_dense_mut(state.component_id).value().get();
            fetch.is_sparse_set = false;
        }
    }
    // static void set_table(Fetch& fetch, State& state, const storage::Table& table) {
    //     if (state.storage_type == StorageType::Table) {
    //         fetch.table_dense   = &table.get_dense_mut(state.component_id).value().get();
    //         fetch.is_sparse_set = false;
    //     }
    // }
    static void set_access(State& state, const FilteredAccess& access) {}
    static void update_access(const State& state, FilteredAccess& access) {
        access.add_component_write(state.component_id);
    }
    static State init_state(WorldCell& world) {
        auto type_id = world.type_registry().type_id<T>();
        return State{.component_id = type_id, .storage_type = storage_for<T>()};
    }
    static std::optional<State> get_state(const Components& components) {
        auto type_id = components.registry().type_id<T>();
        return components.get(type_id).transform([type_id](const ComponentInfo& info) {
            return State{.component_id = type_id, .storage_type = info.storage_type()};
        });
    }
    static bool matches_archetype(const State& state, const archetype::Archetype& archetype) {
        return archetype.contains(state.component_id);
    }
};
static_assert(valid_world_query<WorldQuery<Mut<int>>>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<Mut<T>> {
    using Item                            = Mut<T>;
    using ReadOnly                        = Ref<T>;
    static inline constexpr bool readonly = false;
    static Item fetch(WorldQuery<Mut<T>>::Fetch& fetch, Entity entity, TableRow row) {
        if (fetch.is_sparse_set) {
            return fetch.sparse_set->template get_as_mut<T>(entity)
                .transform([&](T& value) {
                    return Mut<T>(&value, TicksMut::from_refs(fetch.sparse_set->get_tick_refs(entity).value(), 0, 0));
                })
                .value();
        } else {
            return fetch.table_dense->template get_as_mut<T>(row)
                .transform([&](T& value) {
                    return Mut<T>(&value, TicksMut::from_refs(fetch.table_dense->get_tick_refs(row).value(), 0, 0));
                })
                .value();
        }
    }
};
static_assert(valid_query_data<QueryData<Mut<int>>>);

// implements for const T&, in this case, WorldQuery<const T&> is the same as WorldQuery<Ref<T>>
template <typename T>
    requires(!std::is_reference_v<T>)
struct WorldQuery<const T&> : WorldQuery<Ref<std::remove_const_t<T>>> {};
static_assert(valid_world_query<WorldQuery<const int&>>);
template <typename T>
    requires(!std::is_reference_v<T>)
struct QueryData<const T&> : QueryData<Ref<std::remove_const_t<T>>> {
    using Item                            = const T&;
    using ReadOnly                        = const T&;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<const T&>::Fetch& fetch, Entity entity, TableRow row) {
        return QueryData<Ref<std::remove_const_t<T>>>::fetch(fetch, entity, row).get();
    }
};
static_assert(valid_query_data<QueryData<const int&>>);

// implements for T&, in this case, WorldQuery<T&> is the same as WorldQuery<Mut<T>>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<T&> : WorldQuery<Mut<T>> {};
static_assert(valid_world_query<WorldQuery<int&>>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<T&> : QueryData<Mut<T>> {
    using Item                            = T&;
    using ReadOnly                        = const T&;
    static inline constexpr bool readonly = false;
    static Item fetch(WorldQuery<T&>::Fetch& fetch, Entity entity, TableRow row) {
        return QueryData<Mut<T>>::fetch(fetch, entity, row).get();
    }
};
static_assert(valid_query_data<QueryData<int&>>);

/**
 * @brief Optional fetch, returns std::optional<Ref<T>> for Opt<Ref<T>>, std::optional<Mut<T>> for Opt<Mut<T>>, const T*
 * for Opt<const T&>, T* for Opt<T&>.
 */
template <typename T>
    requires valid_world_query<WorldQuery<T>>
struct Opt;

template <typename T>
    requires valid_world_query<WorldQuery<T>>
struct WorldQuery<Opt<T>> {
    struct Fetch {
        typename WorldQuery<T>::Fetch fetch;
        bool matches = false;
    };
    using State = typename WorldQuery<T>::State;
    static Fetch init_fetch(WorldCell& world, const State& state, Tick last_run, Tick this_run) {
        return Fetch{.fetch = WorldQuery<T>::init_fetch(world, state, last_run, this_run), .matches = false};
    }
    static void set_archetype(Fetch& fetch,
                              const State& state,
                              const archetype::Archetype& archetype,
                              const storage::Table& table) {
        fetch.matches = WorldQuery<T>::matches_archetype(state, archetype);
        if (fetch.matches) {
            WorldQuery<T>::set_archetype(fetch.fetch, state, archetype, table);
        }
    }
    // static void set_table(Fetch& fetch, State& state, const storage::Table& table) {
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
    static State init_state(WorldCell& world) { return WorldQuery<T>::init_state(world); }
    static std::optional<State> get_state(const Components& components) { return WorldQuery<T>::get_state(components); }
    static bool matches_archetype(const State& state, const archetype::Archetype& archetype) {
        return true;  // always true, because it is optional
    }
};
static_assert(valid_world_query<WorldQuery<Opt<int&>>>);

template <typename T>
struct AddOptional {
    using type = std::optional<T>;
};
template <typename T>
    requires(std::is_reference_v<T>)
struct AddOptional<T> {
    using type = std::optional<std::reference_wrapper<std::remove_reference_t<T>>>;
};

template <typename T>
    requires valid_world_query<WorldQuery<T>>
struct QueryData<Opt<T>> {
    using Item                            = typename AddOptional<typename QueryData<T>::Item>::type;
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
static_assert(valid_query_data<QueryData<Opt<int&>>>);
static_assert(valid_query_data<QueryData<Opt<Mut<double>>>>);

static_assert(valid_world_query<WorldQuery<Item<int&, const float&, EntityRef, Entity, Ref<double>, Mut<char>>>>);
}  // namespace epix::core::query