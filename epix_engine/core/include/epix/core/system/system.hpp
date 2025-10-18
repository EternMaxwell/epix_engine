#pragma once

#include <concepts>
#include <exception>
#include <memory>
#include <tuple>
#include <type_traits>
#include <variant>

#include "../schedule/system_set.hpp"
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
   private:
    // function table (vtable) for cross-compiler stable dynamic dispatch
    struct Functors {
        // optional marker retained for compatibility
        bool static_functor = true;

        // function pointers -- context is the opaque inner pointer
        std::string_view (*name)(void* ctx)                                                = nullptr;
        void (*set_name)(void* ctx, std::string_view)                                      = nullptr;
        epix::core::meta::type_index (*type_index)(void* ctx)                              = nullptr;
        SystemFlagBits (*flags)(void* ctx)                                                 = nullptr;
        std::expected<Out, RunSystemError> (*run_internal)(void* ctx,
                                                           typename SystemInput<In>::Input input,
                                                           World& world)                   = nullptr;
        std::expected<void, ValidateParamError> (*validate_param)(void* ctx, World& world) = nullptr;
        void (*apply_deferred)(void* ctx, World& world)                                    = nullptr;
        void (*queue_deferred)(void* ctx, DeferredWorld deferred_world)                    = nullptr;
        query::FilteredAccessSet (*initialize)(void* ctx, World& world)                    = nullptr;
        void (*check_change_tick)(void* ctx, Tick tick)                                    = nullptr;
        Tick (*get_last_run)(void* ctx)                                                    = nullptr;
        std::vector<schedule::SystemSetLabel> (*default_sets)(void* ctx)                   = nullptr;
    };

    std::unique_ptr<void, void (*)(void*)> inner;
    Functors const* functors = nullptr;

    template <typename T>
    static const Functors* get_functors() {
        // assert ::name()
        static_assert((requires {
                          { std::declval<T>().name() } -> std::same_as<std::string_view>;
                      }), "System type T must have a name() method returning std::string_view");
        static constexpr bool has_type_index = requires {
            { std::declval<T>().type_index() } -> std::same_as<epix::core::meta::type_index>;
        };  // return type_index of T if not provided
        static constexpr bool has_set_name = requires {
            { std::declval<T>().set_name(std::declval<std::string_view>()) } -> std::same_as<void>;
        };  // optional function
        // assert ::flags()
        static_assert((requires {
                          { std::declval<T>().flags() } -> std::same_as<SystemFlagBits>;
                      }), "System type T must have a flags() method returning SystemFlagBits");
        // assert ::run_internal()
        static_assert((requires {
                          {
                              std::declval<T>().run_internal(std::declval<typename SystemInput<In>::Input>(),
                                                             std::declval<World&>())
                          } -> std::same_as<std::expected<Out, RunSystemError>>;
                      }), "System type T must have a run_internal method with correct signature");
        // assert ::validate_param()
        static_assert((requires {
                          {
                              std::declval<T>().validate_param(std::declval<World&>())
                          } -> std::same_as<std::expected<void, ValidateParamError>>;
                      }), "System type T must have a validate_param method with correct signature");
        // assert ::apply_deferred()
        static_assert((requires {
                          { std::declval<T>().apply_deferred(std::declval<World&>()) } -> std::same_as<void>;
                      }), "System type T must have an apply_deferred method with correct signature");
        // assert ::initialize()
        static_assert((requires {
                          {
                              std::declval<T>().initialize(std::declval<World&>())
                          } -> std::same_as<query::FilteredAccessSet>;
                      }), "System type T must have an initialize method with correct signature");
        // assert ::check_change_tick()
        static_assert((requires {
                          { std::declval<T>().check_change_tick(std::declval<Tick>()) } -> std::same_as<void>;
                      }), "System type T must have a check_change_tick method with correct signature");
        // assert ::get_last_run()
        static_assert((requires {
                          { std::declval<T>().get_last_run() } -> std::same_as<Tick>;
                      }), "System type T must have a get_last_run method with correct signature");
        // assert ::default_sets()
        static_assert((requires {
                          { std::declval<T>().default_sets() } -> std::same_as<std::vector<schedule::SystemSetLabel>>;
                      }), "System type T must have a default_sets method with correct signature");

        static Functors functors = {
            .static_functor = true,
            .name           = [](void* ctx) -> std::string_view { return static_cast<T*>(ctx)->name(); },
            .set_name =
                has_set_name ? [](void* ctx, std::string_view n) { static_cast<T*>(ctx)->set_name(n); } : nullptr,
            .type_index = has_type_index
            ? [](void* ctx) -> epix::core::meta::type_index { return static_cast<T*>(ctx)->type_index(); }
            : [](void* ctx) -> epix::core::meta::type_index { return epix::core::meta::type_id<T>(); },
            .flags        = [](void* ctx) -> SystemFlagBits { return static_cast<T*>(ctx)->flags(); },
            .run_internal = [](void* ctx, typename SystemInput<In>::Input input,
                               World& world) -> std::expected<Out, RunSystemError> {
                return static_cast<T*>(ctx)->run_internal(std::move(input), world);
            },
            .validate_param = [](void* ctx, World& world) -> std::expected<void, ValidateParamError> {
                return static_cast<T*>(ctx)->validate_param(world);
            },
            .apply_deferred = [](void* ctx, World& world) { static_cast<T*>(ctx)->apply_deferred(world); },
            .initialize     = [](void* ctx, World& world) -> query::FilteredAccessSet {
                return static_cast<T*>(ctx)->initialize(world);
            },
            .check_change_tick = [](void* ctx, Tick tick) { static_cast<T*>(ctx)->check_change_tick(tick); },
            .get_last_run      = [](void* ctx) -> Tick { return static_cast<T*>(ctx)->get_last_run(); },
            .default_sets      = [](void* ctx) -> std::vector<schedule::SystemSetLabel> {
                return static_cast<T*>(ctx)->default_sets();
            },
        };
        return &functors;
    }

    std::expected<void, ValidateParamError> validate_param(World& world) {
        return functors->validate_param(inner.get(), world);
    }
    std::expected<Out, RunSystemError> run_internal(SystemInput<In>::Input input, World& world) {
        return functors->run_internal(inner.get(), std::move(input), world);
    }

   public:
    template <typename T>
    System(T* obj)
        : inner(static_cast<void*>(obj), [](void* ptr) { delete static_cast<T*>(ptr); }), functors(get_functors<T>()) {}
    template <typename T>
    System(T* obj, const Functors* f)
        : inner(static_cast<void*>(obj), [](void* ptr) { delete static_cast<T*>(ptr); }), functors(f) {}

    std::string_view name() const { return functors->name(inner.get()); }
    void set_name(std::string_view n) {
        if (functors->set_name) functors->set_name(inner.get(), n);
    }
    epix::core::meta::type_index type_index() const { return functors->type_index(inner.get()); }
    SystemFlagBits flags() const { return functors->flags(inner.get()); }
    bool is_exclusive() const { return (flags() & SystemFlagBits::EXCLUSIVE) != 0; }
    bool is_deferred() const { return (flags() & SystemFlagBits::DEFERRED) != 0; }

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

    void apply_deferred(World& world) { functors->apply_deferred(inner.get(), world); }
    void queue_deferred(DeferredWorld deferred_world) { functors->queue_deferred(inner.get(), deferred_world); }

    query::FilteredAccessSet initialize(World& world) { return functors->initialize(inner.get(), world); }
    void check_change_tick(Tick tick) { functors->check_change_tick(inner.get(), tick); }
    Tick get_last_run() const { return functors->get_last_run(inner.get()); }
    std::vector<schedule::SystemSetLabel> default_sets() const { return functors->default_sets(inner.get()); }

    ~System() {
        if (functors == nullptr) return;
        if (functors->static_functor) return;
        delete functors;
    }
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
struct FunctionSystem {
   public:
    // Storage type to store the orginal function. This is to handle function pointers and lambdas uniformly.
    using Storage = typename function_system_traits<F>::Storage;
    using State   = typename SystemParam<typename function_system_traits<F>::ParamTuple>::State;
    using SInput  = SystemInput<typename function_system_traits<F>::Input>;
    using SParam  = SystemParam<typename function_system_traits<F>::ParamTuple>;

    std::string_view name() const { return meta_.name; }
    void set_name(std::string_view n) { meta_.name = n; }
    epix::core::meta::type_index type_index() const { return type_index_; }
    SystemFlagBits flags() const { return meta_.flags; }
    std::expected<void, ValidateParamError> validate_param(World& world) {
        return SParam::validate_param(state_.value(), meta_, world);
    }
    void apply_deferred(World& world) {
        state_.and_then([&](State& state) -> std::optional<bool> {
            SParam::apply(state, world);
            return std::optional<bool>(true);
        });
    }
    void queue_deferred(DeferredWorld deferred_world) {
        state_.and_then([&](State& state) -> std::optional<bool> {
            SParam::queue(state, deferred_world);
            return std::optional<bool>(true);
        });
    }
    query::FilteredAccessSet initialize(World& world) {
        query::FilteredAccessSet access;
        state_         = SParam::init_state(world);
        meta_.last_run = world.change_tick().relative_to(Tick::max());
        SParam::init_access(*state_, meta_, access, world);
        return access;
    }
    void check_change_tick(Tick tick) { meta_.last_run.check_tick(tick); }
    Tick get_last_run() const { return meta_.last_run; }
    std::vector<schedule::SystemSetLabel> default_sets() const { return {schedule::SystemSetLabel(func_)}; }

    std::expected<typename function_system_traits<F>::Output, RunSystemError> run_internal(typename SInput::Input input,
                                                                                           World& world) {
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

    FunctionSystem(F&& f) : func_(f), type_index_(epix::core::meta::type_id<Storage>()) {
        meta_.name = epix::core::meta::type_id<std::remove_cvref_t<F>>().short_name();
    }
    FunctionSystem(const F& f) : func_(f), type_index_(epix::core::meta::type_id<Storage>()) {
        meta_.name = epix::core::meta::type_id<std::remove_cvref_t<F>>().short_name();
    }

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
System<typename function_system_traits<std::decay_t<F>>::Input,
       typename function_system_traits<std::decay_t<F>>::Output>*
make_system(F&& func) {
    return new System<typename function_system_traits<std::decay_t<F>>::Input,
                      typename function_system_traits<std::decay_t<F>>::Output>(
        new FunctionSystem<std::decay_t<F>>(std::forward<F>(func)));
}
}  // namespace epix::core::system