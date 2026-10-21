#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <epix/traits.hpp>
#include <expected>
#include <tuple>
#include <type_traits>
#include <utility>
#endif

#include <epix/ecs/system/input.hpp>
#include <epix/ecs/system/param.hpp>

// Signature and parameter constraints do not require a complete World/System.
// Observer, World and Commands declarations share this layer without a cycle.
namespace epix::ecs::internal {
template <typename T>
struct system_output {
    using type                                 = T;
    static constexpr bool is_fallible_expected = false;
};

template <typename T, typename E>
struct system_output<std::expected<T, E>> {
    using type                                 = T;
    using error_type                           = E;
    static constexpr bool is_fallible_expected = true;
};

template <typename T>
using system_output_t = typename system_output<T>::type;

// A callable class can expose an unsupported/ambiguous operator(). Inspect its
// member signature first, without instantiating function_traits<F>'s base.
template <typename F, bool = std::is_class_v<std::remove_cvref_t<F>>>
struct system_callable_traits;

template <typename F>
    requires requires {
        typename traits::function_traits<F>::return_type;
        typename traits::function_traits<F>::args_tuple;
    }
struct system_callable_traits<F, false> : traits::function_traits<F> {};

template <typename F>
    requires requires {
        &std::remove_cvref_t<F>::operator();
        typename traits::function_traits<decltype(&std::remove_cvref_t<F>::operator())>::return_type;
        typename traits::function_traits<decltype(&std::remove_cvref_t<F>::operator())>::args_tuple;
    }
struct system_callable_traits<F, true>
    : traits::function_traits<decltype(&std::remove_cvref_t<F>::operator())> {};

template <typename F, typename Tuple>
struct system_callable_invocable : std::false_type {};
template <typename F, typename... Args>
struct system_callable_invocable<F, std::tuple<Args...>> : std::bool_constant<std::is_invocable_v<F, Args...>> {};

template <typename F>
    requires requires {
        typename system_callable_traits<F>::return_type;
        typename system_callable_traits<F>::args_tuple;
    }
struct function_system_traits {
    using Traits                       = system_callable_traits<F>;
    static constexpr std::size_t arity = Traits::arity;
    using Output                       = typename Traits::return_type;
    using ArgsTuple                    = typename Traits::args_tuple;
    using PaddedArgsTuple =
        decltype(std::tuple_cat(std::declval<ArgsTuple>(), std::declval<std::tuple<std::tuple<>>>()));
    using FirstArg = std::tuple_element_t<0, PaddedArgsTuple>;
    // Input is the first argument of the function if it models system_input, otherwise it's std::tuple<>.
    static constexpr bool has_input = arity != 0 && internal::system_input<FirstArg>;
    using Input                     = std::conditional_t<has_input, FirstArg, std::tuple<>>;
    using ParamTuple                = std::remove_pointer_t<decltype([]<std::size_t... I>(std::index_sequence<I...>) {
        return static_cast<std::tuple<std::tuple_element_t<I + (has_input ? 1 : 0), ArgsTuple>...>*>(nullptr);
    }(std::make_index_sequence<arity - (has_input ? 1 : 0)>{}))>;
    using InvocationTuple = std::conditional_t<has_input,
        decltype(std::tuple_cat(std::declval<std::tuple<typename SystemInput<Input>::Param>>(),
                                std::declval<ParamTuple>())), ParamTuple>;
    // The storage type to store the function. function will be stored in pointers,
    // lambdas or other callables will be stored in their own type.
    using Storage = std::decay_t<F>;
};

template <typename F>
concept valid_function_system = requires {
    typename function_system_traits<F>::Storage;
    typename function_system_traits<F>::Input;
    typename function_system_traits<F>::Output;
    // no input will have Input = std::tuple<>, which is a valid_system_input.
    requires internal::system_input<typename function_system_traits<F>::Input>;
    requires system_param<typename function_system_traits<F>::ParamTuple>;
    requires std::constructible_from<typename function_system_traits<F>::Storage, F>;
    requires system_callable_invocable<typename function_system_traits<F>::Storage&,
                                       typename function_system_traits<F>::InvocationTuple>::value;
    { function_system_traits<F>::has_input } -> std::convertible_to<bool>;
};

}  // namespace epix::ecs::internal
