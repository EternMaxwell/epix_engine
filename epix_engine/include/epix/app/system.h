#pragma once

#include <chrono>
#include <memory>

#include "systemparam.h"

namespace epix::app {
struct NotInitializedError {
    std::type_index needed_state;
};
struct UpdateStateFailedError {
    std::vector<std::type_index> failed_args;
};
struct SystemExceptionError {
    // this struct means that the system that is runned has thrown an exception
    std::exception_ptr exception;
};
using RunSystemError = std::
    variant<NotInitializedError, UpdateStateFailedError, SystemExceptionError>;

template <typename Ret>
struct BasicSystem {
    using return_type                                            = Ret;
    virtual void initialize(World& world)                        = 0;
    virtual bool initialized() const noexcept                    = 0;
    virtual std::expected<Ret, RunSystemError> run(World& world) = 0;
    virtual const SystemMeta& get_meta() const                   = 0;
    virtual std::type_index get_data_type() const                = 0;
    // Create a copy of the system. Not initialized.
    virtual BasicSystem* clone() const = 0;
    virtual ~BasicSystem()             = default;
    std::unique_ptr<BasicSystem<Ret>> clone_unique() const {
        return std::unique_ptr<BasicSystem<Ret>>(clone());
    }
    std::shared_ptr<BasicSystem<Ret>> clone_shared() const {
        return std::shared_ptr<BasicSystem<Ret>>(clone());
    }
};
template <typename Ret, typename... Args>
    requires(ValidParam<std::decay_t<Args>> && ...)
struct BasicSystemImpl : public BasicSystem<Ret> {
    using return_type = Ret;
    using param_tuple = std::tuple<SystemParam<std::decay_t<Args>>...>;
    using data_type =
        std::tuple<typename SystemParam<std::decay_t<Args>>::State...>;

   private:
    std::function<Ret(Args...)> m_func;
    std::optional<data_type> data;
    SystemMeta meta;

   public:
    BasicSystemImpl(const std::function<Ret(Args...)>& func) : m_func(func) {}
    BasicSystemImpl(std::function<Ret(Args...)>&& func)
        : m_func(std::move(func)) {}

    BasicSystem<Ret>* clone() const override {
        return new BasicSystemImpl(m_func);
    }

    void initialize(World& world) override {
        meta.world          = &world;
        meta.extract_target = &world;  // Default to the same world
        data.emplace(SystemParam<std::decay_t<Args>>{}.init(world, meta)...);
    }
    bool initialized() const noexcept override { return data.has_value(); }
    const SystemMeta& get_meta() const override { return meta; }
    std::type_index get_data_type() const override { return typeid(data_type); }

    std::expected<Ret, RunSystemError> run(World& world) override {
        if (!data.has_value()) {
            return std::unexpected(NotInitializedError{get_data_type()});
        }
        auto& args = data.value();

        // update the state of each parameter
        auto params = param_tuple{};
        std::vector<std::type_index> failed_args;
        [&]<size_t... I>(std::index_sequence<I...>) {
            ((std::get<I>(params).update(std::get<I>(args), world, meta)
                  ? (void())
                  : (failed_args.emplace_back(
                         typeid(std::decay_t<
                                std::tuple_element_t<I, data_type>>)
                     ),
                     void())),
             ...);
        }(std::make_index_sequence<std::tuple_size_v<data_type>>{});

        // check param validation
        if (!failed_args.empty()) {
            return std::unexpected(UpdateStateFailedError{std::move(failed_args)
            });
        }

        // run the function with the updated parameters
        try {
            if constexpr (std::is_void_v<Ret>) {
                std::apply(
                    m_func,
                    [&params, &args]<size_t... I>(std::index_sequence<I...>) {
                        return std::tie(std::get<I>(params).get(std::get<I>(args
                        ))...);
                    }(std::make_index_sequence<std::tuple_size_v<param_tuple>>()
                    )
                );
                return {};
            } else {
                return std::apply(
                    m_func,
                    [&params, &args]<size_t... I>(std::index_sequence<I...>) {
                        return std::tie(std::get<I>(params).get(std::get<I>(args
                        ))...);
                    }(std::make_index_sequence<std::tuple_size_v<param_tuple>>()
                    )
                );
            }
        } catch (...) {
            return std::unexpected(SystemExceptionError{std::current_exception()
            });
        }
    }
};
// explicit template deduction guide
template <typename Ret, typename... Args>
    requires(ValidParam<std::decay_t<Args>> && ...)
BasicSystemImpl(const std::function<Ret(Args...)>& func
) -> BasicSystemImpl<Ret, Args...>;
template <typename Ret, typename... Args>
    requires(ValidParam<std::decay_t<Args>> && ...)
BasicSystemImpl(std::function<Ret(Args...)>&& func
) -> BasicSystemImpl<Ret, Args...>;

struct IntoSystem {
    template <typename T>
    static auto* into_rawptr(T&& func) {
        std::function fn = std::forward<T>(func);
        using ft         = decltype(fn);
        using ret        = typename ft::result_type;
        return []<typename... Args>(std::function<ret(Args...)>&& f
               ) -> BasicSystem<ret>* {
            return new BasicSystemImpl<ret, Args...>(std::move(f));
        }(std::move(fn));
    }
    template <typename T>
    static auto into_unique(T&& func) {
        auto* raw_ptr = into_rawptr(std::forward<T>(func));
        return []<typename ret>(BasicSystem<ret>* ptr) {
            return std::unique_ptr<BasicSystem<ret>>(ptr);
        }(raw_ptr);
    }
    template <typename T>
    static auto into_shared(T&& func) {
        auto* raw_ptr = into_rawptr(std::forward<T>(func));
        return []<typename ret>(BasicSystem<ret>* ptr) {
            return std::shared_ptr<BasicSystem<ret>>(ptr);
        }(raw_ptr);
    }
    template <typename T>
    static auto into_system(T&& func) {
        return into_unique(std::forward<T>(func)
        );  // default to unique_ptr for simplicity
    }
    template <typename T>
    static BasicSystem<T>* clone(BasicSystem<T>* system) {
        if (!system) {
            return nullptr;
        }
        return system->clone();
    }
    template <typename T>
    static std::unique_ptr<BasicSystem<T>> clone_unique(
        const std::unique_ptr<BasicSystem<T>>& system
    ) {
        if (!system) {
            return nullptr;
        }
        return system->clone_unique();
    }
    template <typename T>
    static std::shared_ptr<BasicSystem<T>> clone_shared(
        const std::shared_ptr<BasicSystem<T>>& system
    ) {
        if (!system) {
            return nullptr;
        }
        return system->clone_shared();
    }
};
}  // namespace epix::app