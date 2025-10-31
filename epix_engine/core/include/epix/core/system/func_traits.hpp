#pragma once

#include <tuple>

namespace epix::core::system {
/// Function traits for functions, function pointers, and lambdas.
template <typename F>
struct function_traits;
template <typename R, typename... Args>
struct function_traits<R(Args...)> {
    using return_type                  = R;
    using args_tuple                   = std::tuple<Args...>;
    static constexpr std::size_t arity = sizeof...(Args);
};
template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};
template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {};
template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {};
template <typename F>
    requires(requires { &F::operator(); })
struct function_traits<F> : function_traits<decltype(&F::operator())> {};
template <typename F>
    requires requires { typename function_traits<F>; }
struct function_traits<F&> : function_traits<F> {};
template <typename F>
    requires requires { typename function_traits<F>; }
struct function_traits<F&&> : function_traits<F> {};
}  // namespace epix::core::system