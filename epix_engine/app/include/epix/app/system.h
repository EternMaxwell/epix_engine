#pragma once

#include "params.h"
#include "tool.h"
#include "world.h"

// pre-declare
namespace epix::app {
struct SystemId;
struct SystemConfig;
template <typename T>
struct ParamResolve;
template <typename T>
struct ParamResolver;
}  // namespace epix::app

// define
namespace epix::app {

template <typename T>
struct FunctionParam;

template <typename Ret, typename... Args>
struct FunctionParam<Ret(Args...)> {
    using type = std::tuple<Args...>;
};

template <typename T>
struct ParamResolve {
    using out_params = GetParams<T>::type;
    using in_params  = GetParams<T>::type;
};

template <typename T>
struct TupleDecay {
    using type = T;
};
template <typename... Args>
struct TupleDecay<std::tuple<Args...>> {
    using type = std::tuple<typename std::decay<Args>::type...>;
};
template <typename T>
struct TupleExtract {
    using type = T;
};
template <typename... Args>
struct TupleExtract<std::tuple<Args...>> {
    using type = std::tuple<Extract<Args>...>;
};

template <typename T>
concept FromSystemParam = requires(T t) {
    {
        std::apply(
            T::from_system_param,
            std::declval<
                typename FunctionParam<decltype(T::from_system_param)>::type>()
        )
    } -> std::same_as<T>;
};

template <FromSystemParam T>
struct ParamResolve<T> {
    using out_params = T;
    using in_params  = typename TupleDecay<
         typename FunctionParam<decltype(T::from_system_param)>::type>::type;
};
template <FromSystemParam T>
struct ParamResolve<Extract<T>> {
    using out_params = Extract<T>;
    using in_params =
        typename TupleExtract<typename ParamResolve<T>::in_params>::type;
};

template <typename T>
struct ParamResolve<Extract<Extract<T>>> {
    using out_params = ParamResolve<T>::out_params;
    using in_params  = ParamResolve<T>::in_params;
};

template <typename T>
struct IsTupleV {
    static constexpr bool value = false;
};
template <typename... Args>
struct IsTupleV<std::tuple<Args...>> {
    static constexpr bool value = true;
};

template <typename... Args>
struct ParamResolve<std::tuple<Args...>> {
    using out_params = std::tuple<typename ParamResolve<Args>::out_params...>;
    using in_params  = std::tuple<typename ParamResolve<Args>::in_params...>;
    template <typename O, typename I>
    struct RootParams {
        using root_params =
            typename RootParams<I, typename ParamResolve<I>::in_params>::
                root_params;
    };
    template <typename T>
    struct RootParams<T, T> {
        using root_params = T;
    };
    using root_params = typename RootParams<out_params, in_params>::root_params;

    template <size_t I>
    static std::tuple_element_t<I, out_params> resolve_i(in_params&& in) {
        using type     = std::tuple_element_t<I, in_params>;
        using out_type = std::tuple_element_t<I, out_params>;
        if constexpr (IsTupleV<type>::value) {
            if constexpr (IsTupleV<out_type>::value) {
                // this is a tuple, so it needs to be resolved recursively
                return ParamResolve<out_type>::resolve(std::move(std::get<I>(in)
                ));
            } else {
                // this is a FromSystemParam, so it should just be constructed
                return std::apply(out_type::from_system_param, std::get<I>(in));
            }
        } else {
            return std::forward<type>(std::get<I>(in));
        }
    }
    template <size_t... I>
    static out_params resolve(in_params&& in, std::index_sequence<I...>) {
        return out_params(resolve_i<I>(std::forward<in_params>(in))...);
    }
    static out_params resolve(in_params&& in) {
        if constexpr (std::same_as<in_params, out_params>) {
            return std::forward<in_params>(in);
        } else {
            return resolve(
                std::forward<in_params>(in), std::index_sequence_for<Args...>()
            );
        }
    }
    static out_params resolve_from_root(root_params& in_addr) {
        if constexpr (std::same_as<root_params, out_params>) {
            return std::forward<root_params>(in_addr);
        } else {
            return resolve(ParamResolve<in_params>::resolve_from_root(in_addr));
        }
    }
};

struct TestParam {
    Commands cmd;
    Res<int> res;
    static TestParam from_system_param(Commands cmd, Res<int> res) {
        return TestParam{cmd, res};
    }
};

static_assert(
    std::same_as<
        std::tuple<std::tuple<Commands, Res<int>>>,
        typename ParamResolve<std::tuple<TestParam>>::root_params>,
    "should be same"
);

// prepare params. now only for resources since it need lock and unlock.
template <typename T>
struct PrepareParam {
    static void prepare(T& t) {};
    static void unprepare(T& t) {};
};

// template <typename T>
// struct PrepareParam<Res<T>> {
//     static void prepare(Res<T>& t) { t.lock(); }
//     static void unprepare(Res<T>& t) { t.unlock(); }
// };
// template <typename T>
// struct PrepareParam<ResMut<T>> {
//     static void prepare(ResMut<T>& t) { t.lock(); }
//     static void unprepare(ResMut<T>& t) { t.unlock(); }
// };

template <typename... Args>
struct PrepareParam<std::tuple<Args...>> {
    template <size_t... I>
    static void prepare(std::tuple<Args...>& t, std::index_sequence<I...>) {
        (PrepareParam<std::tuple_element_t<I, std::tuple<Args...>>>::prepare(
             std::get<I>(t)
         ),
         ...);
    }
    template <size_t... I>
    static void unprepare(std::tuple<Args...>& t, std::index_sequence<I...>) {
        (PrepareParam<std::tuple_element_t<I, std::tuple<Args...>>>::unprepare(
             std::get<I>(t)
         ),
         ...);
    }
    static void prepare(std::tuple<Args...>& t) {
        prepare(t, std::index_sequence_for<Args...>());
    }
    static void unprepare(std::tuple<Args...>& t) {
        unprepare(t, std::index_sequence_for<Args...>());
    }
};

template <typename... Args>
struct ParamResolver<std::tuple<Args...>> {
    using param_data_t =
        typename ParamResolve<std::tuple<std::decay_t<Args>...>>::root_params;

   private:
    param_data_t m_param_data;

   public:
    ParamResolver(World* src, World* dst, LocalData* local_data)
        : m_param_data(GetParams<param_data_t>::get(src, dst, local_data)) {}

    auto resolve() {
        return ParamResolve<
            std::tuple<std::decay_t<Args>...>>::resolve_from_root(m_param_data);
    };
};
}  // namespace epix::app