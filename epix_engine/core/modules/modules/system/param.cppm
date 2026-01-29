module;

export module epix.core:system.param;

import std;
import epix.meta;

import :query;
import :world.interface;
import :world.entity_ref;

namespace core {
export template <typename T>
struct SystemParam;

export struct ValidateParamError {
    meta::type_index param_type;
    std::string message;  // An optional message, cause some error can be understood just with type name.
};

enum SystemFlagBits : std::uint8_t {
    EXCLUSIVE = 1 << 0,  // system requires exclusive access to the world
    DEFERRED  = 1 << 1,  // system has deferred commands.
};
export struct SystemMeta {
    std::string name;
    SystemFlagBits flags = (SystemFlagBits)0;
    Tick last_run        = 0;

    bool is_exclusive() const { return (SystemFlagBits::EXCLUSIVE & flags) != (SystemFlagBits)0; }
    bool is_deferred() const { return (SystemFlagBits::DEFERRED & flags) != (SystemFlagBits)0; }
};

export template <typename T>
concept system_param = requires(World& world, SystemMeta& meta, FilteredAccessSet& access) {
    // used to store data that persists across system runs
    typename SystemParam<T>::State;
    requires std::movable<typename SystemParam<T>::State>;
    // the item type returned when accessing the param, the item returned may not be T itself, it may be reference.
    typename SystemParam<T>::Item;
    requires std::same_as<T, typename SystemParam<T>::Item>;

    { SystemParam<T>::init_state(world) } -> std::same_as<typename SystemParam<T>::State>;
    requires requires(const typename SystemParam<T>::State& state, typename SystemParam<T>::State& state_mut,
                      DeferredWorld deferred_world, Tick tick, const Archetype& archetype) {
        { SystemParam<T>::init_access(state, meta, access, world) } -> std::same_as<void>;
        { SystemParam<T>::new_archetype(state_mut, archetype, meta) } -> std::same_as<void>;
        { SystemParam<T>::apply(state_mut, std::as_const(meta), world) } -> std::same_as<void>;
        { SystemParam<T>::queue(state_mut, std::as_const(meta), deferred_world) } -> std::same_as<void>;
        {
            SystemParam<T>::validate_param(state_mut, std::as_const(meta), world)
        } -> std::same_as<std::expected<void, ValidateParamError>>;
        {
            SystemParam<T>::get_param(state_mut, std::as_const(meta), world, tick)
        } -> std::same_as<typename SystemParam<T>::Item>;
    };
};

// A base struct to provide default implementation for some functions.
export struct ParamBase {
    static void init_access(const auto&, SystemMeta&, FilteredAccessSet&, const World&) {}
    static void new_archetype(auto&, const Archetype&, SystemMeta&) {}
    static void apply(auto&, const SystemMeta&, World&) {}
    static void queue(auto&, const SystemMeta&, DeferredWorld) {}
    static std::expected<void, ValidateParamError> validate_param(auto&, const SystemMeta&, World&) { return {}; }
};

template <query_data D, query_filter F>
struct SystemParam<Query<D, F>> : ParamBase {
    using State = QueryState<D, F>;
    using Item  = Query<D, F>;
    static State init_state(World& world) { return QueryState<D, F>::create(world); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        AccessConflicts conflicts = access.get_conflicts(state.component_access());
        if (!conflicts.empty()) {
            throw std::runtime_error(std::format(
                "Query<{}, {}> in system [{}] has access conflicts with previous params, with conflicts on ids: {}.",
                meta::type_id<D>().name(), meta.name, meta::type_id<F>().name(), conflicts.to_string()));
        }
        access.add(state.component_access());
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return state.query_with_ticks(world, meta.last_run, tick);
    }
};
static_assert(system_param<Query<int&, With<float>>>);

template <query_data D, query_filter F>
struct SystemParam<Single<D, F>> : SystemParam<Query<D, F>> {
    using Base  = SystemParam<Query<D, F>>;
    using State = typename Base::State;
    using Item  = Single<D, F>;
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return Single<D, F>(Base::get_param(state, meta, world, tick).single().value());
    }
    static std::expected<void, ValidateParamError> validate_param(const State& state,
                                                                  const SystemMeta& meta,
                                                                  World& world) {
        Query<D, F> query = Base::get_param(state, meta, world, world.change_tick());
        if (!query.single().has_value()) {
            return std::unexpected(ValidateParamError{
                .param_type = meta::type_id<Single<D, F>>(),
                .message    = "Associated Query for Single system param is empty.",
            });
        }
    }
};
static_assert(system_param<Single<int&, With<float>>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct SystemParam<Res<T>> : ParamBase {
    using State = TypeId;
    using Item  = Res<T>;
    static State init_state(World& world) { return world.type_registry().type_id<T>(); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_write(state)) {
            throw std::runtime_error(std::format(
                "Res<{}> in system [{}] has access conflicts with a previous ResMut<{}>. Consider removing this param.",
                meta::type_id<T>().name(), meta.name, state.get(), meta::type_id<T>().name()));
        }
        access.add_unfiltered_resource_read(state);
    }
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta&, World& world) {
        return world.storage()
            .resources.get(state)
            .transform([](const ResourceData& res) -> std::expected<void, ValidateParamError> {
                if (res.is_present()) return {};
                return std::unexpected(ValidateParamError{
                    .param_type = meta::type_id<Res<T>>(),
                    .message    = "Res storage exists, value not present.",
                });
            })
            .value_or(std::unexpected(ValidateParamError{
                .param_type = meta::type_id<Res<T>>(),
                .message    = "Res storage do not exists.",
            }));
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return world.storage()
            .resources.get(state)
            .and_then([&](const ResourceData& res) {
                return res.get_as<T>().transform([&](const T& value) {
                    return Res<T>(std::addressof(value),
                                  Ticks::from_refs(res.get_tick_refs().value(), meta.last_run, tick));
                });
            })
            .value();
    }
};
static_assert(system_param<Res<int>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct SystemParam<ResMut<T>> : ParamBase {
    using State = TypeId;
    using Item  = ResMut<T>;
    static State init_state(World& world) { return world.type_registry().type_id<T>(); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_read(state)) {
            throw std::runtime_error(
                std::format("ResMut<{}> in system [{}] has access conflicts with a previous Res<{}> or ResMut<{}>.",
                            meta::type_id<T>().name(), meta.name, state.get(), meta::type_id<T>().name(),
                            meta::type_id<T>().name()));
        }
        access.add_unfiltered_resource_write(state);
    }
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta&, World& world) {
        return world.storage()
            .resources.get(state)
            .transform([](const ResourceData& res) -> std::expected<void, ValidateParamError> {
                if (res.is_present()) return {};
                return std::unexpected(ValidateParamError{
                    .param_type = meta::type_id<Res<T>>(),
                    .message    = "ResMut storage exists, value not present.",
                });
            })
            .value_or(std::unexpected(ValidateParamError{
                .param_type = meta::type_id<Res<T>>(),
                .message    = "ResMut storage do not exists.",
            }));
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return world.storage_mut()
            .resources.get_mut(state)
            .and_then([&](ResourceData& res) {
                return res.get_as_mut<T>().transform([&](T& value) {
                    return ResMut<T>(std::addressof(value),
                                     TicksMut::from_refs(res.get_tick_refs().value(), meta.last_run, tick));
                });
            })
            .value();
    }
};
static_assert(system_param<ResMut<int>>);

template <>
struct SystemParam<const World&> : ParamBase {
    using State = std::tuple<>;
    using Item  = const World&;
    static State init_state(World&) { return {}; }
    static void init_access(const State&, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        FilteredAccess world_access = FilteredAccess::matches_everything();
        world_access.access_mut().read_all();
        //? Are we going to disallow any mutable access to the world when this param is used?
        if (!access.get_conflicts(world_access).empty()) {
            throw std::runtime_error(
                std::format("const World& in system [{}] has access conflicts with previous params.", meta.name));
        }
        access.add(world_access);
    }
    static Item get_param(State&, const SystemMeta&, World& world, Tick) { return world; }
};
template <>
struct SystemParam<World&> : ParamBase {
    using State = std::tuple<>;
    using Item  = World&;
    static State init_state(World&) { return {}; }
    static void init_access(const State&, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        FilteredAccess world_access = FilteredAccess::matches_everything();
        world_access.access_mut().write_all();
        //? Are we going to disallow any access to the world when this param is used?
        if (!access.get_conflicts(world_access).empty()) {
            throw std::runtime_error(
                std::format("World& in system [{}] has access conflicts with previous params.", meta.name));
        }
        access.add(world_access);
    }
    static Item get_param(State&, const SystemMeta&, World& world, Tick) { return world; }
};
static_assert(system_param<World&>);
static_assert(system_param<const World&>);

template <>
struct SystemParam<DeferredWorld> : ParamBase {
    using State = std::tuple<>;
    using Item  = DeferredWorld;
    static State init_state(World&) { return {}; }
    static void init_access(const State&, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        meta.flags = (SystemFlagBits)(meta.flags | SystemFlagBits::DEFERRED);

        //? Are we going to disallow any access to the world when this param is used?
        if (access.combined_access().has_any_read()) {
            throw std::runtime_error(
                std::format("DeferredWorld in system [{}] has access conflicts with previous params.", meta.name));
        }
        access.write_all();
    }
    static Item get_param(State&, const SystemMeta&, World& world, Tick) { return DeferredWorld(world); }
};
static_assert(system_param<DeferredWorld>);

export template <typename T>
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
    static Item get_param(State& state, const SystemMeta&, World&, Tick) { return Local<T>(const_cast<T&>(state)); }
};
static_assert(system_param<Local<int>>);

template <system_param T>
struct SystemParam<std::optional<T>> : SystemParam<T> {
    using Base  = SystemParam<T>;
    using State = typename Base::State;
    // It is currently useless to have optional param for reference types, since they will always be present like World&
    using Item = std::optional<typename Base::Item>;
    static std::expected<void, ValidateParamError> validate_param(const State& state,
                                                                  const SystemMeta& meta,
                                                                  World& world) {
        return {};
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        if (Base::validate_param(state, meta, world)) {
            return Base::get_param(state, meta, world, tick);
        } else {
            return std::nullopt;
        }
    }
};
template <typename T>
    requires system_param<T&>
struct SystemParam<std::optional<std::reference_wrapper<T>>> : SystemParam<T&> {
    using Base  = SystemParam<T&>;
    using State = typename Base::State;
    using Item  = std::optional<std::reference_wrapper<T>>;
    static std::expected<void, ValidateParamError> validate_param(const State& state,
                                                                  const SystemMeta& meta,
                                                                  World& world) {
        return {};
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        if (Base::validate_param(state, meta, world)) {
            return std::ref(Base::get_param(state, meta, world, tick));
        } else {
            return std::nullopt;
        }
    }
};
static_assert(system_param<std::optional<Res<int>>>);

template <system_param... T>
struct SystemParam<std::tuple<T...>> {
    using State = std::tuple<typename SystemParam<T>::State...>;
    using Item  = std::tuple<typename SystemParam<T>::Item...>;
    static State init_state(World& world) { return State(SystemParam<T>::init_state(world)...); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world) {
        []<std::size_t... I>(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world,
                             std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::init_access(std::get<I>(state), meta, access,
                                                                                 world),
             ...);
        }(state, meta, access, world, std::index_sequence_for<T...>{});
    }
    static void new_archetype(State& state, const Archetype& archetype, SystemMeta& meta) {
        []<std::size_t... I>(State& state, const Archetype& archetype, SystemMeta& meta, std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::new_archetype(std::get<I>(state), archetype, meta),
             ...);
        }(state, archetype, meta, std::index_sequence_for<T...>{});
    }
    static void apply(State& state, const SystemMeta& meta, World& world) {
        []<std::size_t... I>(State& state, const SystemMeta& meta, World& world, std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::apply(std::get<I>(state), meta, world), ...);
        }(state, meta, world, std::index_sequence_for<T...>{});
    }
    static void queue(State& state, const SystemMeta& meta, DeferredWorld deferred_world) {
        []<std::size_t... I>(State& state, const SystemMeta& meta, DeferredWorld deferred_world,
                             std::index_sequence<I...>) {
            (SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::queue(std::get<I>(state), meta, deferred_world),
             ...);
        }(state, meta, deferred_world, std::index_sequence_for<T...>{});
    }
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta& meta, World& world) {
        return []<std::size_t I>(this auto&& self, State& state, const SystemMeta& meta, World& world,
                                 std::integral_constant<std::size_t, I>) -> std::expected<void, ValidateParamError> {
            if constexpr (I >= sizeof...(T))
                return {};
            else
                return SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::validate_param(std::get<I>(state), meta,
                                                                                              world)
                    .and_then([&] { return self(state, meta, world, std::integral_constant<std::size_t, I + 1>{}); });
        }(state, meta, world, std::integral_constant<std::size_t, 0>{});
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return []<std::size_t... I>(State& state, const SystemMeta& meta, World& world, Tick tick,
                                    std::index_sequence<I...>) {
            return Item(SystemParam<std::tuple_element_t<I, std::tuple<T...>>>::get_param(std::get<I>(state), meta,
                                                                                          world, tick)...);
        }(state, meta, world, tick, std::index_sequence_for<T...>{});
    }
};
static_assert(
    system_param<
        std::tuple<const World&, Res<int>, std::optional<ResMut<float>>, Query<int&, With<float>>, Local<float>>>);

export template <system_param... Ts>
struct ParamSet {
   public:
    using State = std::tuple<typename SystemParam<Ts>::State...>;

    template <std::size_t I>
    typename SystemParam<std::tuple<Ts...>>::Item get() {
        return SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::get_param(std::get<I>(states_), *meta_, *world_,
                                                                                  change_tick_);
    }
    typename SystemParam<std::tuple<Ts...>>::Item get() {
        return SystemParam<std::tuple<Ts...>>::get_param(*states_, *meta_, *world_, change_tick_);
    }

   private:
    State* states_;
    World* world_;
    const SystemMeta* meta_;
    Tick change_tick_;

    ParamSet(State* states, World* world, const SystemMeta* meta, Tick change_tick)
        : states_(states), world_(world), meta_(meta), change_tick_(change_tick) {}

    friend struct SystemParam<ParamSet<Ts...>>;
};
template <system_param... Ts>
struct SystemParam<ParamSet<Ts...>> : SystemParam<std::tuple<Ts...>> {
    using Base  = SystemParam<std::tuple<Ts...>>;
    using State = typename Base::State;
    using Item  = ParamSet<Ts...>;
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world) {
        []<std::size_t... I>(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world,
                             std::index_sequence<I...>) {
            (
                []<std::size_t J>(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world,
                                  std::integral_constant<std::size_t, J>) {
                    FilteredAccessSet access_copy = access;
                    SystemParam<std::tuple_element_t<J, std::tuple<Ts...>>>::init_access(std::get<J>(state), meta,
                                                                                         access_copy, world);
                }(state, meta, access, world, std::integral_constant<std::size_t, I>{}),
                ...);
        }(state, meta, access, world, std::index_sequence_for<Ts...>{});
        []<std::size_t... I>(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world,
                             std::index_sequence<I...>) {
            (
                []<std::size_t J>(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world,
                                  std::integral_constant<std::size_t, J>) {
                    FilteredAccessSet new_access;
                    SystemParam<std::tuple_element_t<J, std::tuple<Ts...>>>::init_access(std::get<J>(state), meta,
                                                                                         new_access, world);
                    access.extend(new_access);
                }(state, meta, access, world, std::integral_constant<std::size_t, I>{}),
                ...);
        }(state, meta, access, world, std::index_sequence_for<Ts...>{});
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return Item(&state, &world, &meta, tick);
    }
};
static_assert(system_param<
              ParamSet<const World&, Res<int>, std::optional<ResMut<float>>, Query<int&, With<float>>, Local<float>>>);

template <typename T>
struct SystemBuffer;
template <typename T>
concept system_buffer = requires {
    requires requires(T& buffer, const SystemMeta& meta, World& world) {
        { SystemBuffer<T>::apply(buffer, meta, world) } -> std::same_as<void>;
        { SystemBuffer<T>::queue(buffer, meta, DeferredWorld(world)) } -> std::same_as<void>;
    };
};

template <system_buffer T>
struct Deferred {
   public:
    Deferred(T& buffer) : buffer_(std::addressof(buffer)) {}
    T& get() { return *buffer_; }
    T* operator->() { return buffer_; }
    T& operator*() { return *buffer_; }

   private:
    T* buffer_;
};
template <system_buffer F>
    requires is_from_world<F>
struct SystemParam<Deferred<F>> : ParamBase {
    using State = F;
    using Item  = Deferred<F>;
    static State init_state(World& world) { return FromWorld<F>::create(world); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        meta.flags = (SystemFlagBits)(meta.flags | SystemFlagBits::DEFERRED);
        // No access is added since deferred params do not access anything immediately.
    }
    static void apply(State& state, const SystemMeta& meta, World& world) {
        SystemBuffer<F>::apply(state, meta, world);
    }
    static void queue(State& state, const SystemMeta& meta, DeferredWorld deferred_world) {
        SystemBuffer<F>::queue(state, meta, deferred_world);
    }
    static Item get_param(State& state, const SystemMeta&, World&, Tick) { return Deferred<F>(state); }
};

template <>
struct SystemParam<const Entities&> : ParamBase {
    using State = std::tuple<>;
    using Item  = const Entities&;
    static State init_state(World&) { return {}; }
    static Item get_param(State&, const SystemMeta&, World& world, Tick) { return world.entities(); }
};
static_assert(system_param<const Entities&>);
}  // namespace core