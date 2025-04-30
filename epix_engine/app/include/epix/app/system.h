#pragma once

#include <chrono>

#include "systemparam.h"

namespace epix::app {
template <typename... Args>
struct ParamResolver {
    SystemParam<std::tuple<Args...>> system_param;

    ParamResolver(World& src, World& dst, LocalData& locals) {
        system_param.complete(src, dst, locals);
        system_param.prepare();
    }
    ~ParamResolver() { system_param.unprepare(); }
    std::tuple<Args&...> resolve() { return system_param.get(); }
};
template <typename Ret>
struct BasicSystem {
    using return_type = Ret;

   private:
    std::function<Ret(World*, World*, BasicSystem*)> m_func;
    LocalData m_locals;
    SystemParamInfo m_src_info, m_dst_info;

    double time_cost = 1.0;  // in milliseconds
    double time_avg  = 1.0;  // in milliseconds
    double factor    = 0.1;  // for time_avg

   public:
    template <typename... Args>
    BasicSystem(const std::function<Ret(Args...)>& func)
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              ParamResolver<std::decay_t<Args>...> param_resolver(
                  *src, *dst, sys->m_locals
              );
              return std::apply(func, param_resolver.resolve());
          }),
          time_cost(1.0),
          time_avg(1.0) {
        SystemParam<std::tuple<std::decay_t<Args>...>>::write_info(
            m_src_info, m_dst_info
        );
    }
    template <typename... Args>
    BasicSystem(Ret (*func)(Args...))
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              ParamResolver<std::decay_t<Args>...> param_resolver(
                  *src, *dst, sys->m_locals
              );
              return std::apply(func, param_resolver.resolve());
          }),
          time_cost(1.0),
          time_avg(1.0) {
        SystemParam<std::tuple<std::decay_t<Args>...>>::write_info(
            m_src_info, m_dst_info
        );
    }
    template <typename T>
        requires(!std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> &&
                 !epix::util::type_traits::
                     specialization_of<std::decay_t<T>, std::function>) &&
                requires { std::function(std::declval<T>()); }
    BasicSystem(T&& func) : BasicSystem(std::function(std::forward<T>(func))) {}

    template <typename U>
    bool conflict_with(const BasicSystem<U>& other) const {
        if (m_src_info.conflict_with(other.m_src_info) ||
            m_dst_info.conflict_with(other.m_dst_info)) {
            return true;
        }
        return false;
    }

    Ret run(World& src, World& dst) {
        struct Timer {
            std::chrono::high_resolution_clock::time_point start;
            BasicSystem* sys;
            Timer(BasicSystem* sys)
                : start(std::chrono::high_resolution_clock::now()), sys(sys) {}
            ~Timer() {
                auto end = std::chrono::high_resolution_clock::now();
                auto delta =
                    std::chrono::duration_cast<
                        std::chrono::duration<double, std::milli>>(end - start)
                        .count();
                sys->time_cost = delta;
                sys->time_avg =
                    sys->factor * delta + (1.0 - sys->factor) * sys->time_avg;
            }
        };
        Timer timer(this);
        return m_func(&src, &dst, this);
    }

    double get_time_cost() const { return time_cost; }
    double get_time_avg() const { return time_avg; }

    const SystemParamInfo& get_src_info() const { return m_src_info; }
    const SystemParamInfo& get_dst_info() const { return m_dst_info; }

    template <typename U>
    friend struct BasicSystem;
};
}  // namespace epix::app