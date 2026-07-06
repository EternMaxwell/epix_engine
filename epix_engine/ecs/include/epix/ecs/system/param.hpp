#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#endif

#include <epix/ecs/archetype.hpp>
#include <epix/ecs/query/access.hpp>
#include <epix/ecs/world/decl.hpp>
#include <epix/ecs/world/from_world.hpp>

// #include <epix/ecs/world/entity_ref.hpp>
// #include <epix/ecs/world/interface.hpp>

namespace epix::ecs {
/** @brief Trait class defining how a type is used as a system parameter.
 *  Specialize to define State, Item, init_state(), get_param(), etc. */
EPIX_EXPORT template <typename T>
struct SystemParam;

/** @brief Error returned when a system parameter fails validation before execution. */
EPIX_EXPORT struct ValidateParamError {
    /** @brief Type of the parameter that failed validation. */
    meta::type_index param_type;
    /** @brief Optional descriptive message about the validation failure. */
    std::string message;
};
EPIX_EXPORT enum SystemFlagBits : std::uint8_t {
    EXCLUSIVE = 1 << 0,  // system requires exclusive access to the world
    DEFERRED  = 1 << 1,  // system has deferred commands.
};
/** @brief Metadata about a system, including its name, flags, and last-run tick. */
EPIX_EXPORT struct SystemMeta {
    /** @brief Human-readable system name. */
    std::string name;
    /** @brief Bitwise flags (exclusive, deferred). */
    SystemFlagBits flags = (SystemFlagBits)0;
    /** @brief Tick when this system last ran. */
    Tick last_run = 0;

    /** @brief Check if this system requires exclusive world access. */
    bool is_exclusive() const noexcept { return (SystemFlagBits::EXCLUSIVE & flags) != (SystemFlagBits)0; }
    /** @brief Check if this system produces deferred commands. */
    bool is_deferred() const noexcept { return (SystemFlagBits::DEFERRED & flags) != (SystemFlagBits)0; }
};

/** @brief Concept for types usable as system parameters.
 *  Requires SystemParam specialization with State, Item, init_state, get_param, etc. */
EPIX_EXPORT template <typename T>
concept system_param = requires(World& world, SystemMeta& meta, FilteredAccessSet& access) {
    typename SystemParam<T>::State;
    requires std::movable<typename SystemParam<T>::State>;
    typename SystemParam<T>::Item;
    requires std::same_as<T, typename SystemParam<T>::Item>;
    typename std::bool_constant<SystemParam<T>::readonly>;

    { SystemParam<T>::init_state(world) } -> std::same_as<typename SystemParam<T>::State>;
    requires requires(const typename SystemParam<T>::State& state, typename SystemParam<T>::State& state_mut,
                      DeferredWorld deferred_world, Tick tick, const Archetype& archetype) {
        { SystemParam<T>::init_access(state, meta, access, std::as_const(world)) } -> std::same_as<void>;
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

/** @brief Concept for read-only system parameters that don't mutate the world. */
EPIX_EXPORT template <typename T>
concept readonly_system_param = system_param<T> && SystemParam<T>::readonly;

/** @brief SystemParam adapter for read-only parameters, accepting const World&. */
EPIX_EXPORT template <readonly_system_param T>
struct ROSystemParam : SystemParam<T> {
    using State = typename SystemParam<T>::State;
    using Item  = typename SystemParam<T>::Item;

    static std::expected<void, ValidateParamError> validate_param(State& state,
                                                                  const SystemMeta& meta,
                                                                  const World& world) {
        return SystemParam<T>::validate_param(state, meta, const_cast<World&>(world));
    }
    static Item get_param(State& state, const SystemMeta& meta, const World& world, Tick tick) {
        return SystemParam<T>::get_param(state, meta, const_cast<World&>(world), tick);
    }
};

/** @brief Base struct providing default no-op implementations for SystemParam static methods. */
EPIX_EXPORT struct ParamBase {
    static void init_access(const auto&, SystemMeta&, FilteredAccessSet&, const World&) noexcept {}
    static void new_archetype(auto&, const Archetype&, SystemMeta&) noexcept {}
    static void apply(auto&, const SystemMeta&, World&) noexcept {}
    static void queue(auto&, const SystemMeta&, DeferredWorld&) noexcept {}
    static std::expected<void, ValidateParamError> validate_param(auto&, const SystemMeta&, World&) noexcept {
        return {};
    }
};

template <>
struct SystemParam<const World&> : ParamBase {
    using State                    = std::tuple<>;
    using Item                     = const World&;
    static constexpr bool readonly = true;
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
    using State                    = std::tuple<>;
    using Item                     = World&;
    static constexpr bool readonly = false;
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

template <system_param T>
struct SystemParam<std::optional<T>> : SystemParam<T> {
    using Base  = SystemParam<T>;
    using State = typename Base::State;
    // It is currently useless to have optional param for reference types, since they will always be present like World&
    using Item                     = std::optional<typename Base::Item>;
    static constexpr bool readonly = Base::readonly;
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
    using Base                     = SystemParam<T&>;
    using State                    = typename Base::State;
    using Item                     = std::optional<std::reference_wrapper<T>>;
    static constexpr bool readonly = Base::readonly;
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

template <system_param... T>
struct SystemParam<std::tuple<T...>> {
    using State                    = std::tuple<typename SystemParam<T>::State...>;
    using Item                     = std::tuple<typename SystemParam<T>::Item...>;
    static constexpr bool readonly = (SystemParam<T>::readonly && ...);
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
    static void queue(State& state, const SystemMeta& meta, DeferredWorld& deferred_world) {
        []<std::size_t... I>(State& state, const SystemMeta& meta, DeferredWorld& deferred_world,
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

/** @brief Groups multiple system parameters for deferred/manual access.
 *  Use get<I>() to retrieve individual params or get() for all.
 *  @tparam Ts System parameter types. */
EPIX_EXPORT template <system_param... Ts>
struct ParamSet {
   public:
    using State = std::tuple<typename SystemParam<Ts>::State...>;

    template <std::size_t I>
    typename SystemParam<std::tuple_element_t<I, std::tuple<Ts...>>>::Item get() {
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
    using Base                     = SystemParam<std::tuple<Ts...>>;
    using State                    = typename Base::State;
    using Item                     = ParamSet<Ts...>;
    static constexpr bool readonly = Base::readonly;
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

/** @brief Trait struct that defines how a system buffer is applied and queued.
 * @tparam T The buffer type.
 *
 * Users must specialize this struct and provide:
 * - `static void apply(T& buffer, const SystemMeta& meta, World& world);`
 * - `static void queue(T& buffer, const SystemMeta& meta, DeferredWorld world);`
 */
EPIX_EXPORT template <typename T>
struct SystemBuffer;
namespace internal {
/** @brief Concept satisfied by types that have a valid SystemBuffer specialization. */
template <typename T>
concept system_buffer = requires {
    requires requires(T& buffer, const SystemMeta& meta, World& world, DeferredWorld& deferred_world) {
        { SystemBuffer<T>::apply(buffer, meta, world) } -> std::same_as<void>;
        { SystemBuffer<T>::queue(buffer, meta, deferred_world) } -> std::same_as<void>;
    };
};
}  // namespace internal

/** @brief Wrapper for deferred system buffers.
 *
 * Deferred parameters do not immediately access the World. Instead, they
 * accumulate work in a buffer that is applied later during flush.
 * @tparam T A type satisfying the system_buffer concept.
 */
template <internal::system_buffer T>
struct Deferred {
   public:
    Deferred(T& buffer) : buffer_(std::addressof(buffer)) {}
    /** @brief Get a reference to the underlying buffer. */
    T& get() { return *buffer_; }
    /** @brief Arrow operator for accessing buffer members. */
    T* operator->() { return buffer_; }
    /** @brief Dereference operator for accessing the buffer. */
    T& operator*() { return *buffer_; }

   private:
    T* buffer_;
};
template <internal::system_buffer F>
    requires internal::is_from_world<F>
struct SystemParam<Deferred<F>> : ParamBase {
    using State                    = F;
    using Item                     = Deferred<F>;
    static constexpr bool readonly = true;
    static State init_state(World& world) { return internal::FromWorld<F>::create(world); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        meta.flags = (SystemFlagBits)(meta.flags | SystemFlagBits::DEFERRED);
        // No access is added since deferred params do not access anything immediately.
    }
    static void apply(State& state, const SystemMeta& meta, World& world) {
        SystemBuffer<F>::apply(state, meta, world);
    }
    static void queue(State& state, const SystemMeta& meta, DeferredWorld& deferred_world) {
        SystemBuffer<F>::queue(state, meta, deferred_world);
    }
    static Item get_param(State& state, const SystemMeta&, World&, Tick) { return Deferred<F>(state); }
};

template <>
struct SystemParam<const Entities&> : ParamBase {
    using State                    = std::tuple<>;
    using Item                     = const Entities&;
    static constexpr bool readonly = true;
    static State init_state(World&) { return {}; }
    static Item get_param(State&, const SystemMeta&, World& world, Tick) { return internal::world_entities(world); }
};
static_assert(system_param<const Entities&>);
}  // namespace epix::ecs