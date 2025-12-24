module;

#include <cstdint>
#include <tuple>
#include <type_traits>

export module epix.traits:function;

export {
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
        requires requires {
            typename function_traits<F>;
            typename function_traits<F>::return_type;
            typename function_traits<F>::args_tuple;
        }
    struct function_traits<F&> : function_traits<F> {};
    template <typename F>
        requires requires {
            typename function_traits<F>;
            typename function_traits<F>::return_type;
            typename function_traits<F>::args_tuple;
        }
    struct function_traits<F&&> : function_traits<F> {};
}