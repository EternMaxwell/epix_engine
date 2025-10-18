#pragma once

#include <exception>
#include <tuple>
#include <type_traits>
#include <variant>

#include "func_traits.hpp"
#include "input.hpp"
#include "param.hpp"

namespace epix::core::system {
struct SystemException {
    std::exception_ptr exception;
};
using RunSystemError = std::variant<ValidateParamError, SystemException>;
template <typename In, typename Out>
struct System {
    virtual std::string_view name() const                   = 0;
    virtual epix::core::meta::type_index type_index() const = 0;
    virtual SystemFlagBits flags() const                    = 0;
    bool is_exclusive() const { return (flags() & SystemFlagBits::EXCLUSIVE) != 0; }
    bool is_deferred() const { return (flags() & SystemFlagBits::DEFERRED) != 0; }
    virtual std::expected<Out, RunSystemError> run_internal(SystemInput<In>::Input input, World& world) = 0;
    virtual std::expected<void, ValidateParamError> validate_param(World& world)                        = 0;
    virtual void apply_deferred(World& world)                                                           = 0;
    virtual void queue_deferred(DeferredWorld deferred_world)                                           = 0;
    std::expected<Out, RunSystemError> run(SystemInput<In>::Input input, World& world) {
        auto res = validate_param(world)
                       .transform_error([](ValidateParamError&& err) -> RunSystemError { return std::move(err); })
                       .and_then([&] { return run_internal(std::move(input), world); });
        if (res.has_value()) apply_deferred(world);
        return std::move(res);
    }
    std::expected<Out, RunSystemError> run_no_apply(SystemInput<In>::Input input, World& world) {
        validate_param(world)
            .transform_error([](ValidateParamError&& err) -> RunSystemError { return std::move(err); })
            .and_then([&] { return run_internal(std::move(input), world); });
    }
    virtual query::FilteredAccessSet initialize(World& world) = 0;
    virtual void check_change_tick(Tick tick)                 = 0;
    virtual Tick get_last_run() const                         = 0;

    virtual ~System() = default;
};
template <typename In = std::tuple<>, typename Out = void>
using SystemUnique = std::unique_ptr<System<In, Out>>;

template <typename F>
struct function_system_traits {
    using Traits                  = function_traits<F>;
    static constexpr size_t arity = Traits::arity;
    using Output                  = typename Traits::return_type;
    // Input is the first argument of the function if it modules valid_system_input, otherwise it's std::tuple<>.
    using Input                     = std::remove_reference_t<decltype(*([] {
        if constexpr (arity == 0) {
            // no arguments -> input is an empty tuple
            return static_cast<std::tuple<>*>(nullptr);
        } else {
            using FirstArg = std::tuple_element_t<0, typename Traits::args_tuple>;
            if constexpr (valid_system_input<SystemInput<FirstArg>>) {
                return static_cast<FirstArg*>(nullptr);
            } else {
                return static_cast<std::tuple<>*>(nullptr);
            }
        }
    }()))>;
    static constexpr bool has_input = [] {
        // we are not just checking Input != std::tuple<>, because the input type may be just std::tuple<>.
        if constexpr (arity == 0) {
            return false;
        } else {
            using FirstArg = std::tuple_element_t<0, typename Traits::args_tuple>;
            if constexpr (valid_system_input<SystemInput<FirstArg>>) {
                return true;
            } else {
                return false;
            }
        }
    }();
    using ParamTuple = std::remove_reference_t<decltype(*([] {
        if constexpr (has_input) {
            // has input, so params are from second argument to the last argument.
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return static_cast<std::tuple<std::tuple_element_t<I + 1, typename Traits::args_tuple>...>*>(nullptr);
            }(std::make_index_sequence<arity - 1>{});
        } else {
            return static_cast<typename Traits::args_tuple*>(nullptr);
        }
    }()))>;
    // The storage type to store the function. function will be stored in pointers,
    // lambdas or other callables will be stored in their own type.
    using Storage = std::decay_t<F>;
};

template <typename F>
    requires requires {
        typename function_system_traits<F>::Input;
        typename function_system_traits<F>::Output;
        // no input will have Input = std::tuple<>, which is a valid_system_input.
        requires valid_system_input<SystemInput<typename function_system_traits<F>::Input>>;
        requires valid_system_param<SystemParam<typename function_system_traits<F>::ParamTuple>>;
        { function_system_traits<F>::has_input } -> std::convertible_to<bool>;
    }
struct FunctionSystem
    : public System<typename function_system_traits<F>::Input, typename function_system_traits<F>::Output> {
   public:
    // Storage type to store the orginal function. This is to handle function pointers and lambdas uniformly.
    using Storage = typename function_system_traits<F>::Storage;
    using State   = typename SystemParam<typename function_system_traits<F>::ParamTuple>::State;
    using SInput  = SystemInput<typename function_system_traits<F>::Input>;
    using SParam  = SystemParam<typename function_system_traits<F>::ParamTuple>;

    std::string_view name() const override { return meta_.name; }
    epix::core::meta::type_index type_index() const override { return type_index_; }
    SystemFlagBits flags() const override { return meta_.flags; }
    std::expected<void, ValidateParamError> validate_param(World& world) override {
        return SParam::validate_param(state_.value(), meta_, world);
    }
    void apply_deferred(World& world) override {
        state_.and_then([&](State& state) -> std::optional<bool> {
            SParam::apply(state, world);
            return std::optional<bool>(true);
        });
    }
    void queue_deferred(DeferredWorld deferred_world) override {
        state_.and_then([&](State& state) -> std::optional<bool> {
            SParam::queue(state, deferred_world);
            return std::optional<bool>(true);
        });
    }
    query::FilteredAccessSet initialize(World& world) override {
        query::FilteredAccessSet access;
        state_         = SParam::init_state(world);
        meta_.last_run = world.change_tick().relative_to(Tick::max());
        SParam::init_access(*state_, meta_, access, world);
        return access;
    }
    void check_change_tick(Tick tick) override { meta_.last_run.check_tick(tick); }
    Tick get_last_run() const override { return meta_.last_run; }

    std::expected<typename function_system_traits<F>::Output, RunSystemError> run_internal(typename SInput::Input input,
                                                                                           World& world) override {
        struct TickSpan {
            Tick* tick;
            World* world;
            ~TickSpan() { *tick = world->change_tick(); }
        };
        auto call = [](auto&& f,
                       auto&& t) -> std::expected<typename function_system_traits<F>::Output, RunSystemError> {
            try {
                if constexpr (std::same_as<void, typename function_system_traits<F>::Output>) {
                    std::apply(f, std::move(t));
                    return {};
                } else {
                    return std::apply(f, std::move(t));
                }
            } catch (...) {
                return std::unexpected(SystemException{
                    .exception = std::current_exception(),
                });
            }
        };
        TickSpan span{
            .tick  = &meta_.last_run,
            .world = &world,
        };
        if constexpr (function_system_traits<F>::has_input) {
            return call(func_, std::tuple_cat(std::forward_as_tuple(std::forward<typename SInput::Input>(input)),
                                              SParam::get_param(*state_, meta_, world, world.change_tick())));
        } else {
            return call(func_, SParam::get_param(*state_, meta_, world, world.change_tick()));
        }
    }

    FunctionSystem(F&& f) : func_(f), type_index_(epix::core::meta::type_id<Storage>()) {}
    FunctionSystem(const F& f) : func_(f), type_index_(epix::core::meta::type_id<Storage>()) {}

   private:
    Storage func_;
    std::optional<State> state_;
    SystemMeta meta_;
    epix::core::meta::type_index type_index_;
};

template <typename F>
    requires requires {
        typename function_system_traits<std::decay_t<F>>::Input;
        typename function_system_traits<std::decay_t<F>>::Output;
        // no input will have Input = std::tuple<>, which is a valid_system_input.
        requires valid_system_input<SystemInput<typename function_system_traits<std::decay_t<F>>::Input>>;
        requires valid_system_param<SystemParam<typename function_system_traits<std::decay_t<F>>::ParamTuple>>;
        { function_system_traits<std::decay_t<F>>::has_input } -> std::convertible_to<bool>;
    }
System<typename function_system_traits<F>::Input, typename function_system_traits<F>::Output>* make_system(F&& func) {
    return new FunctionSystem<std::decay_t<F>>(std::forward<F>(func));
}
}  // namespace epix::core::system