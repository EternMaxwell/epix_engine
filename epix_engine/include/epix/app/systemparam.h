#pragma once

#include <concepts>
#include <cstdint>
#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>
#include <optional>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>

#include "world.h"

namespace epix::app {
template <typename T>
struct has_static_from_helper {
    template <typename>
    static auto test(...) -> std::false_type;
    template <typename U>
    static auto test(int) -> decltype(&U::from_param, std::true_type{});
    static constexpr bool value = decltype(test<T>(0))::value;
};
template <typename T>
    requires(has_static_from_helper<T>::value)
struct from_param_traits {
    using type = decltype(&T::from_param);
    template <typename U>
    struct function_traits;
    template <typename Ret, typename... Args>
    struct function_traits<Ret (*)(Args...)> {
        using return_type = Ret;
        using args_tuple  = std::tuple<Args...>;
    };
    template <typename U>
    struct tuple_decay;
    template <typename... Args>
    struct tuple_decay<std::tuple<Args...>> {
        using type = std::tuple<std::decay_t<Args>...>;
    };
    using return_type        = typename function_traits<type>::return_type;
    using args_tuple         = typename function_traits<type>::args_tuple;
    using decayed_args_tuple = typename tuple_decay<args_tuple>::type;
};
template <typename T>
struct Extract : public T {
    using value_type = T;
    using T::T;
    Extract(const T& t) : T(t) {}
    Extract(T&& t) : T(std::move(t)) {}
    Extract(const Extract& other) : T(other) {}
    Extract(Extract&& other) : T(std::move(other)) {}
    Extract& operator=(const T& other) {
        static_cast<T&>(*this) = other;
        return *this;
    }
    Extract& operator=(T&& other) {
        static_cast<T&>(*this) = std::move(other);
        return *this;
    }
};
template <>
struct Extract<World> {
    using value_type = World&;
    World& world;
    Extract(World& world) : world(world) {}
    World& operator*() { return world; }
    World* operator->() { return &world; }
    operator World&() { return world; }
};

struct ExtractTarget {
   private:
    World* m_world;

    ExtractTarget(World& world) : m_world(&world) {}

    friend struct App;

   public:
    World& get_world() { return *m_world; }
};

template <typename T>
struct Local {
   private:
    T* m_ptr;

   public:
    Local(T* ptr) : m_ptr(ptr) {}
    Local(const Local&)            = default;
    Local(Local&&)                 = default;
    Local& operator=(const Local&) = default;
    Local& operator=(Local&&)      = default;
    ~Local()                       = default;

    T& operator*() { return *m_ptr; }
    T* operator->() { return m_ptr; }
    const T& operator*() const { return *m_ptr; }
    const T* operator->() const { return m_ptr; }
};

struct Access {
    bool commands = false;
    struct queries_t {
        entt::dense_set<std::type_index> component_reads;
        entt::dense_set<std::type_index> component_writes;
        entt::dense_set<std::type_index> component_excludes;
    };
    std::vector<queries_t> queries;
    entt::dense_set<std::type_index> resource_reads;
    entt::dense_set<std::type_index> resource_writes;
    bool reads_all  = false;
    bool writes_all = false;
};

struct SystemMeta {
    Access access;
    World* world          = nullptr;
    World* extract_target = nullptr;

    EPIX_API static bool conflict(const SystemMeta& a, const SystemMeta& b);
};

template <typename T>
struct SystemParam;

template <typename T>
concept ValidParam =
    requires(SystemParam<T> p) {
        typename SystemParam<T>::State;
        {
            p.init(std::declval<World&>(), std::declval<SystemMeta&>())
        } -> std::same_as<typename SystemParam<T>::State>;
        {
            p.update(
                std::declval<typename SystemParam<T>::State&>(),
                std::declval<World&>(), std::declval<const SystemMeta&>()
            )
        } -> std::same_as<bool>;
        {
            p.get(std::declval<typename SystemParam<T>::State&>())
        } -> std::same_as<T&>;
    } && std::is_empty_v<SystemParam<T>> &&
    std::is_default_constructible_v<SystemParam<T>>;
template <typename T>
static inline constexpr bool tuple_valid_param = false;
template <typename... Ts>
static inline constexpr bool tuple_valid_param<std::tuple<Ts...>> =
    ((ValidParam<Ts> && ...));

template <ValidParam T>
struct SystemParam<Extract<T>> : public SystemParam<T> {
    using State =
        std::pair<std::optional<Extract<T>>, typename SystemParam<T>::State>;
    State init(World& world, SystemMeta& meta) {
        auto& target = world.get_resource<ExtractTarget>()->get_world();
        return {std::nullopt, SystemParam<T>::init(target, meta)};
    }
    bool update(State& state, World& world, const SystemMeta& meta) {
        auto& target = world.get_resource<ExtractTarget>()->get_world();
        bool inner   = SystemParam<T>::update(state.second, target, meta);
        if (inner) {
            state.first.emplace(SystemParam<T>::get(state.second));
        }
        return state.first.has_value();
    }
    Extract<T>& get(State& state) {
        if (!state.first.has_value()) {
            throw std::runtime_error(
                "Cannot create Extract<T> from SystemParam state: inner update "
                "failed."
            );
        }
        return state.first.value();
    }
};

template <typename T>
concept FromParam =
    has_static_from_helper<T>::value &&
    tuple_valid_param<typename from_param_traits<T>::decayed_args_tuple> &&
    (std::same_as<typename from_param_traits<T>::return_type, T> ||
     std::
         same_as<typename from_param_traits<T>::return_type, std::optional<T>>);

template <FromParam T>
struct SystemParam<T> {
    using from_param_traits  = from_param_traits<T>;
    using decayed_args_tuple = typename from_param_traits::decayed_args_tuple;
    using from_return_type   = typename from_param_traits::return_type;
    using args_tuple         = typename from_param_traits::args_tuple;
    template <typename... Args>
    static auto cast_to_param_tuple(std::tuple<Args...>& args
    ) -> std::tuple<SystemParam<std::decay_t<Args>>...>;
    template <typename... Args>
    static auto cast_to_state_tuple(std::tuple<Args...>& args
    ) -> std::tuple<typename SystemParam<std::decay_t<Args>>::State...>;
    using param_tuple =
        decltype(cast_to_param_tuple(std::declval<args_tuple&>()));
    using state_tuple =
        decltype(cast_to_state_tuple(std::declval<args_tuple&>()));
    using State = std::pair<std::optional<T>, state_tuple>;
    State init(World& world, SystemMeta& meta) {
        if constexpr (std::tuple_size_v<param_tuple> > 0) {
            return {
                std::nullopt,
                std::apply(
                    [&world, &meta](auto&&... inner_param) {
                        return state_tuple(inner_param.init(world, meta)...);
                    },
                    param_tuple{}
                )
            };
        } else {
            return {std::nullopt, state_tuple{}};
        }
    }
    bool update(State& state, World& world, const SystemMeta& meta) {
        param_tuple inner_params;
        bool inner = [&]<size_t... I>(std::index_sequence<I...>) -> bool {
            return (
                std::get<I>(inner_params)
                    .update(std::get<I>(state.second), world, meta) &&
                ...
            );
        }(std::make_index_sequence<std::tuple_size_v<param_tuple>>{});
        auto& param = state.first;
        if (inner) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                if constexpr (std::same_as<from_return_type, T>) {
                    param.emplace(
                        T::from_param(std::get<I>(inner_params)
                                          .get(std::get<I>(state.second))...)
                    );
                } else if constexpr (std::same_as<
                                         from_return_type, std::optional<T>>) {
                    param.swap(T::from_param(std::get<I>(inner_params)
                                                 .get(std::get<I>(state.second)
                                                 )...));
                }
            }(std::make_index_sequence<std::tuple_size_v<args_tuple>>{});
        } else {
            param.reset();
        }
        return param.has_value();
    }
    T& get(State& state) {
        if (!state.first.has_value()) {
            throw std::runtime_error(
                "Cannot create T from SystemParam state: inner update failed."
            );
        }
        return state.first.value();
    }
};
template <FromParam T>
struct SystemParam<std::optional<T>> : public SystemParam<T> {
    using State = typename SystemParam<T>::State;
    bool update(State& state, World& world, const SystemMeta& meta) {
        SystemParam<T>::update(state, world, meta);
        return true;
    }
    std::optional<T>& get(State& state) { return state.first; }
};

template <typename T>
struct SystemParam<Local<T>> {
    using State = std::pair<T, Local<T>>;
    State init(World& world, SystemMeta& meta) {
        if constexpr (std::constructible_from<T, World&>) {
            return {T(world), Local<T>(nullptr)};
        } else if constexpr (FromWorld<T>) {
            return {T::from_world(world), Local<T>(nullptr)};
        } else {
            static_assert(
                std::is_default_constructible_v<T>,
                "Local<T> requires T to be default constructible or FromWorld"
            );
            return {T(), Local<T>(nullptr)};
        }
    }
    bool update(State& state, World& world, const SystemMeta& meta) {
        return true;  // Local params do not need update
    }
    Local<T>& get(State& state) {
        state.second = Local<T>(&state.first);
        return state.second;
    }
};
template <>
struct SystemParam<World> {
    using State = World*;
    State init(World& world, SystemMeta& meta) {
        meta.access.reads_all  = true;
        meta.access.writes_all = true;
        return &world;
    }
    bool update(State& state, World& world, const SystemMeta& meta) {
        state = &world;
        return true;  // World param does not need update
    }
    World& get(State& state) {
        if (!state) {
            throw std::runtime_error("World param is not initialized.");
        }
        return *state;
    }
};
static_assert(
    ValidParam<Local<int>>, "Local<int> should be a valid SystemParam type."
);
static_assert(ValidParam<World>, "World should be a valid SystemParam type.");
}  // namespace epix::app