#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "../archetype.hpp"
#include "../query/access.hpp"
#include "../query/query.hpp"
#include "../storage/resource.hpp"
#include "../world.hpp"

namespace epix::core::system {
template <typename T>
struct SystemParam;
template <typename T>
struct param_type;
template <typename T>
struct param_type<SystemParam<T>> {
    using type = T;
};

enum SystemFlagBits : uint8_t {
    EXCLUSIVE = 1 << 0,  // system requires exclusive access to the world
    DEFERRED  = 1 << 1,  // system has deferred commands.
};
struct SystemMeta {
    std::string name;
    SystemFlagBits flags = (SystemFlagBits)0;
    Tick last_run        = 0;

    bool is_exclusive() const { return (SystemFlagBits::EXCLUSIVE & flags) != (SystemFlagBits)0; }
    bool is_deferred() const { return (SystemFlagBits::DEFERRED & flags) != (SystemFlagBits)0; }
};

template <typename T>
concept valid_system_param = requires(World& world, SystemMeta& meta, query::FilteredAccessSet& access) {
    typename param_type<T>::type;
    // used to store data that persists across system runs
    typename T::State;
    // the item type returned when accessing the param, the item returned may not be T itself, it may be reference.
    typename T::Item;
    std::same_as<typename param_type<T>::type, typename T::Item>;

    { T::init_state(world) } -> std::same_as<typename T::State>;
    requires requires(const typename T::State& state, typename T::State& state_mut, DeferredWorld deferred_world,
                      Tick tick, const archetype::Archetype& archetype) {
        { T::init_access(state, meta, access, world) } -> std::same_as<void>;
        { T::new_archetype(state_mut, archetype, meta) } -> std::same_as<void>;
        { T::apply(state_mut, world) } -> std::same_as<void>;
        { T::queue(state_mut, deferred_world) } -> std::same_as<void>;
        { T::validate_param(state_mut, std::as_const(meta), world) } -> std::same_as<bool>;
        { T::get_param(state, std::as_const(meta), world, tick) } -> std::same_as<typename T::Item>;
    };
};
// A base struct to provide default implementation for some functions.
struct ParamBase {
    static void init_access(const auto&, SystemMeta&, query::FilteredAccessSet&, const World&) {}
    static void new_archetype(auto&, const archetype::Archetype&, SystemMeta&) {}
    static void apply(auto&, World&) {}
    static void queue(auto&, DeferredWorld) {}
    static bool validate_param(auto&, const SystemMeta&, World&) { return true; }
};

template <typename D, typename F>
    requires(query::valid_query_data<query::QueryData<D>> && query::valid_query_filter<query::QueryFilter<F>>)
struct SystemParam<query::Query<D, F>> : ParamBase {
    using State = query::QueryState<D, F>;
    using Item  = query::Query<D, F>;
    static State init_state(World& world) { return query::QueryState<D, F>::create(world); }
    static void init_access(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World&) {
        query::AccessConflicts conflicts = access.get_conflicts(state.component_access());
        if (!conflicts.empty()) {
            throw std::runtime_error(
                "Query<{}, {}> in system [{}] has access conflicts with previous params, with conflicts on ids: {}.",
                epix::core::meta::type_name<D>(), meta.name, epix::core::meta::type_name<F>(), conflicts.to_string());
        }
        access.add(state.component_access());
    }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        return state.query_with_ticks(world, meta.last_run, tick);
    }
};
static_assert(valid_system_param<SystemParam<query::Query<int&, query::With<float>>>>);
template <typename D, typename F>
    requires(query::valid_query_data<query::QueryData<D>> && query::valid_query_filter<query::QueryFilter<F>>)
struct SystemParam<query::Single<D, F>> : SystemParam<query::Query<D, F>> {
    using Base  = SystemParam<query::Query<D, F>>;
    using State = typename Base::State;
    using Item  = query::Single<D, F>;
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        return query::Single<D, F>(Base::get_param(state, meta, world, tick).single().value());
    }
    static bool validate_param(const State& state, const SystemMeta& meta, World& world) {
        Query<D, F> query = Base::get_param(state, meta, world, world.change_tick());
        return query.single().has_value();
    }
};
static_assert(valid_system_param<SystemParam<query::Single<int&, query::With<float>>>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct SystemParam<Res<T>> : ParamBase {
    using State = TypeId;
    using Item  = Res<T>;
    static State init_state(World& world) { return world.type_registry().type_id<T>(); }
    static void init_access(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_write(state)) {
            throw std::runtime_error(
                "Res<{}> in system [{}] has access conflicts with a previous ResMut<{}>. Consider removing this param.",
                epix::core::meta::type_name<T>(), meta.name, state.get(), epix::core::meta::type_name<T>());
        }
        access.add_unfiltered_resource_read(state);
    }
    static bool validate_param(State& state, const SystemMeta&, World& world) {
        return world.storage()
            .resources.get(state)
            .and_then([](const storage::ResourceData& res) { return res.is_present(); })
            .value_or(false);
    }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        return world.storage()
            .resources.get(state)
            .and_then([&](const storage::ResourceData& res) {
                return res.get_as<T>().transform([&](const T& value) {
                    return Res<T>(&value, Ticks::from_refs(res.get_tick_refs().value(), meta.last_run, tick));
                });
            })
            .value();
    }
};
static_assert(valid_system_param<SystemParam<Res<int>>>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct SystemParam<ResMut<T>> : ParamBase {
    using State = TypeId;
    using Item  = ResMut<T>;
    static State init_state(World& world) { return world.type_registry().type_id<T>(); }
    static void init_access(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_read(state)) {
            throw std::runtime_error(
                "ResMut<{}> in system [{}] has access conflicts with a previous Res<{}> or ResMut<{}>.",
                epix::core::meta::type_name<T>(), meta.name, state.get(), epix::core::meta::type_name<T>(),
                epix::core::meta::type_name<T>());
        }
        access.add_unfiltered_resource_write(state);
    }
    static bool validate_param(State& state, const SystemMeta&, World& world) {
        return world.storage()
            .resources.get(state)
            .and_then([](const storage::ResourceData& res) { return res.is_present(); })
            .value_or(false);
    }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        return world.storage()
            .resources.get(state)
            .and_then([&](storage::ResourceData& res) {
                return res.get_as_mut<T>().transform([&](T& value) {
                    return ResMut<T>(&value, TicksMut::from_refs(res.get_tick_refs().value(), meta.last_run, tick));
                });
            })
            .value();
    }
};
static_assert(valid_system_param<SystemParam<ResMut<int>>>);

template <>
struct SystemParam<const World&> : ParamBase {
    using State = std::tuple<>;
    using Item  = const World&;
    static State init_state(World&) { return {}; }
    static void init_access(const State&, SystemMeta& meta, query::FilteredAccessSet& access, const World&) {
        query::FilteredAccess world_access = query::FilteredAccess::matches_everything();
        world_access.access_mut().read_all();
        //? Are we going to disallow any mutable access to the world when this param is used?
        if (!access.get_conflicts(world_access).empty()) {
            throw std::runtime_error(
                std::format("const World& in system [{}] has access conflicts with previous params.", meta.name));
        }
        access.add(world_access);
    }
    static Item get_param(const State&, const SystemMeta&, World& world, Tick) { return world; }
};
template <>
struct SystemParam<World&> : ParamBase {
    using State = std::tuple<>;
    using Item  = World&;
    static State init_state(World&) { return {}; }
    static void init_access(const State&, SystemMeta& meta, query::FilteredAccessSet& access, const World&) {
        query::FilteredAccess world_access = query::FilteredAccess::matches_everything();
        world_access.access_mut().write_all();
        //? Are we going to disallow any access to the world when this param is used?
        if (!access.get_conflicts(world_access).empty()) {
            throw std::runtime_error(
                std::format("World& in system [{}] has access conflicts with previous params.", meta.name));
        }
        access.add(world_access);
    }
    static Item get_param(const State&, const SystemMeta&, World& world, Tick) { return world; }
};
static_assert(valid_system_param<SystemParam<World&>>);
static_assert(valid_system_param<SystemParam<const World&>>);

template <>
struct SystemParam<DeferredWorld> : ParamBase {
    using State = std::tuple<>;
    using Item  = DeferredWorld;
    static State init_state(World&) { return {}; }
    static void init_access(const State&, SystemMeta& meta, query::FilteredAccessSet& access, const World&) {
        meta.flags = (SystemFlagBits)(meta.flags | SystemFlagBits::DEFERRED);

        //? Are we going to disallow any access to the world when this param is used?
        if (access.combined_access().has_any_read()) {
            throw std::runtime_error(
                std::format("DeferredWorld in system [{}] has access conflicts with previous params.", meta.name));
        }
        access.write_all();
    }
    static Item get_param(const State&, const SystemMeta&, World& world, Tick) { return DeferredWorld(world); }
};
static_assert(valid_system_param<SystemParam<DeferredWorld>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct Local {
   public:
    Local(T& value) : value(std::addressof(value)) {}

    T& get() { return *value; }
    T* operator->() { return value; }
    T& operator*() { return *value; }
    operator T&() { return *value; }

    const T& get() const { return *value; }
    const T* operator->() const { return value; }
    const T& operator*() const { return *value; }
    operator const T&() const { return *value; }

   private:
    T* value;
};

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T> && is_from_world<T>)
struct SystemParam<Local<T>> : ParamBase {
    using State = T;
    using Item  = Local<T>;
    static State init_state(World& world) { return FromWorld<T>::create(world); }
    static Item get_param(const State& state, const SystemMeta&, World&, Tick) {
        return Local<T>(const_cast<T&>(state));
    }
};
static_assert(valid_system_param<SystemParam<Local<int>>>);

template <typename T>
    requires valid_system_param<SystemParam<T>>
struct SystemParam<std::optional<T>> : SystemParam<T> {
    using Base  = SystemParam<T>;
    using State = typename Base::State;
    // It is currently useless to have optional param for reference types, since they will always be present like World&
    using Item = std::optional<typename Base::Item>;
    static bool validate_param(const State& state, const SystemMeta& meta, World& world) { return true; }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        if (Base::validate_param(state, meta, world)) {
            return Base::get_param(state, meta, world, tick);
        } else {
            return std::nullopt;
        }
    }
};
template <typename T>
    requires valid_system_param<SystemParam<T&>>
struct SystemParam<std::optional<std::reference_wrapper<T>>> : SystemParam<T&> {
    using Base  = SystemParam<T&>;
    using State = typename Base::State;
    using Item  = std::optional<std::reference_wrapper<T>>;
    static bool validate_param(const State& state, const SystemMeta& meta, World& world) { return true; }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        if (Base::validate_param(state, meta, world)) {
            return std::ref(Base::get_param(state, meta, world, tick));
        } else {
            return std::nullopt;
        }
    }
};

template <typename... T>
    requires((valid_system_param<SystemParam<T>> && ...))
struct SystemParam<std::tuple<T...>> {
    using State = std::tuple<typename SystemParam<T>::State...>;
    using Item  = std::tuple<typename SystemParam<T>::Item...>;
    static State init_state(World& world) { return State(SystemParam<T>::init_state(world)...); }
    static void init_access(const State& state,
                            SystemMeta& meta,
                            query::FilteredAccessSet& access,
                            const World& world) {
        []<std::size_t... I>(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World& world,
                             std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::init_access(std::get<I>(state), meta, access,
                                                                                 world),
             ...);
        }(std::index_sequence_for<T...>{});
    }
    static void new_archetype(State& state, const archetype::Archetype& archetype, SystemMeta& meta) {
        []<std::size_t... I>(State& state, const archetype::Archetype& archetype, SystemMeta& meta,
                             std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::new_archetype(std::get<I>(state), archetype, meta),
             ...);
        }(std::index_sequence_for<T...>{});
    }
    static void apply(State& state, World& world) {
        []<std::size_t... I>(State& state, World& world, std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::apply(std::get<I>(state), world), ...);
        }(std::index_sequence_for<T...>{});
    }
    static void queue(State& state, DeferredWorld deferred_world) {
        []<std::size_t... I>(State& state, DeferredWorld deferred_world, std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::queue(std::get<I>(state), deferred_world), ...);
        }(std::index_sequence_for<T...>{});
    }
    static bool validate_param(State& state, const SystemMeta& meta, World& world) {
        return []<std::size_t... I>(State& state, const SystemMeta& meta, World& world, std::index_sequence<I...>) {
            return (true && (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::validate_param(std::get<I>(state),
                                                                                                    meta, world) &&
                             ...));
        }(std::index_sequence_for<T...>{}, state, meta, world);
    }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        return Item(SystemParam<T>::get_param(std::get<typename SystemParam<T>::State>(state), meta, world, tick)...);
    }
};
static_assert(valid_system_param<SystemParam<std::tuple<const World&,
                                                        Res<int>,
                                                        std::optional<ResMut<float>>,
                                                        query::Query<int&, query::With<float>>,
                                                        Local<float>>>>);

template <typename... Ts>
    requires(valid_system_param<SystemParam<Ts>> && ...)
struct ParamSet {
   public:
    using State = typename ParamSet<std::tuple<Ts...>>::State;

    template <std::size_t I>
    typename SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::Item get(World& world, Tick tick) {
        return std::get<I>(states_).get_param(std::get<I>(states_), meta_, *world_, tick);
    }

   private:
    State states_;
    World* world_;
    SystemMeta meta_;
    Tick change_tick_;

    ParamSet(const State& states, World* world, const SystemMeta& meta, Tick change_tick)
        : states_(states), world_(world), meta_(meta), change_tick_(change_tick) {}

    friend struct SystemParam<ParamSet<Ts...>>;
};
template <typename... Ts>
    requires(valid_system_param<SystemParam<Ts>> && ...)
struct SystemParam<ParamSet<Ts...>> {
    using State = typename SystemParam<std::tuple<Ts...>>::State;
    using Item  = ParamSet<Ts...>;
    static State init_state(World& world) { return State(SystemParam<Ts>::init_state(world)...); }
    static void init_access(const State& state,
                            SystemMeta& meta,
                            query::FilteredAccessSet& access,
                            const World& world) {
        []<std::size_t... I>(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World& world,
                             std::index_sequence<I...>) {
            (
                []<size_t J>(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World& world,
                             std::integral_constant<size_t, J>) {
                    query::FilteredAccessSet access_copy = access;
                    SystemParam<std::tuple_element_t<J, std::tuple<Ts...>>>::init_access(std::get<J>(state), meta,
                                                                                         access_copy, world);
                }(state, meta, access, world, std::integral_constant<size_t, I>{}),
                ...);
        }(state, meta, access, world, std::index_sequence_for<Ts...>{});
        []<std::size_t... I>(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World& world,
                             std::index_sequence<I...>) {
            (
                []<size_t J>(const State& state, SystemMeta& meta, query::FilteredAccessSet& access, const World& world,
                             std::integral_constant<size_t, J>) {
                    query::FilteredAccessSet new_access;
                    SystemParam<std::tuple_element_t<J, std::tuple<Ts...>>>::init_access(std::get<J>(state), meta,
                                                                                         new_access, world);
                    access.extend(new_access);
                }(state, meta, access, world, std::integral_constant<size_t, I>{}),
                ...);
        }(state, meta, access, world, std::index_sequence_for<Ts...>{});
    }
    static void new_archetype(State& state, const archetype::Archetype& archetype, SystemMeta& meta) {
        []<std::size_t... I>(State& state, const archetype::Archetype& archetype, SystemMeta& meta,
                             std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::new_archetype(std::get<I>(state), archetype,
                                                                                    meta),
             ...);
        }(std::index_sequence_for<Ts...>{});
    }
    static void apply(State& state, World& world) {
        []<std::size_t... I>(State& state, World& world, std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::apply(std::get<I>(state), world), ...);
        }(std::index_sequence_for<Ts...>{});
    }
    static void queue(State& state, DeferredWorld deferred_world) {
        []<std::size_t... I>(State& state, DeferredWorld deferred_world, std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::queue(std::get<I>(state), deferred_world), ...);
        }(std::index_sequence_for<Ts...>{});
    }
    static bool validate_param(State& state, const SystemMeta& meta, World& world) {
        return []<std::size_t... I>(State& state, const SystemMeta& meta, World& world, std::index_sequence<I...>) {
            return (true && (SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::validate_param(std::get<I>(state),
                                                                                                     meta, world) &&
                             ...));
        }(std::index_sequence_for<Ts...>{}, state, meta, world);
    }
    static Item get_param(const State& state, const SystemMeta& meta, World& world, Tick tick) {
        return Item(state, &world, meta, tick);
    }
};
}  // namespace epix::core::system