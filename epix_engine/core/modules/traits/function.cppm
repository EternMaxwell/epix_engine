module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <tuple>
#endif
export module epix.traits:function;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export {
    /** @brief Trait that extracts return type, argument types, and arity from callable types.
     * @tparam F The function/callable type to introspect.
     *
     * Provides:
     * - `return_type` — the function's return type
     * - `args_tuple` — a std::tuple of argument types
     * - `arity` — the number of arguments
     * - `class_type` — (member functions only) the owning class
     */
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
    struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {
        using class_type = C;
    };
    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {
        using class_type = const C;
    };
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