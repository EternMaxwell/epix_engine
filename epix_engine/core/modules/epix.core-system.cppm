/**
 * @file epix.core-system.cppm
 * @brief System partition for system execution
 */

export module epix.core:system;

import :fwd;
import :entities;
import :type_system;
import :query;
import :change_detection;

#include <concepts>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export namespace epix::core::system {
    // System parameter trait
    template <typename T>
    struct SystemParam;
    
    // Function traits for system functions
    template <typename F>
    struct function_traits;
    
    template <typename R, typename... Args>
    struct function_traits<R(Args...)> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
    };
    
    template <typename R, typename... Args>
    struct function_traits<R(*)(Args...)> : function_traits<R(Args...)> {};
    
    template <typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...)> : function_traits<R(Args...)> {};
    
    template <typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...) const> : function_traits<R(Args...)> {};
    
    template <typename F>
        requires requires { &F::operator(); }
    struct function_traits<F> : function_traits<decltype(&F::operator())> {};
    
    // Commands for deferred entity/component operations
    struct Commands;
    struct EntityCommands;
    
    // Local storage for systems
    template <typename T>
    struct Local;
    
    // Parameter set for exclusive access
    template <typename... Ts>
    struct ParamSet;
    
    // System metadata
    struct SystemMeta {
        std::string_view name;
        Access access;
    };
    
    // System errors
    struct ValidateParamError {
        std::string message;
    };
    
    struct RunSystemError {
        std::string message;
    };
    
    struct SystemException : std::exception {
        std::string message;
        const char* what() const noexcept override { return message.c_str(); }
    };
    
    // System interface
    struct System {
        virtual ~System() = default;
        virtual void run(World& world) = 0;
        virtual SystemMeta meta() const = 0;
    };
    
    // System construction helpers
    template <typename F>
    std::unique_ptr<System> make_system(F&& func);
    
    template <typename F>
    std::shared_ptr<System> make_system_shared(F&& func);
    
    template <typename F>
    std::unique_ptr<System> make_system_unique(F&& func);
}  // namespace epix::core::system
