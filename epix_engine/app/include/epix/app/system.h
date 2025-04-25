#pragma once

#include "params.h"
#include "tool.h"
#include "world.h"

// pre-declare
namespace epix::app {
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

struct SystemParamInfo {
    bool has_world   = false;
    bool has_command = false;
    bool has_query   = false;
    std::vector<std::tuple<
        entt::dense_set<std::type_index>,
        entt::dense_set<std::type_index>,
        entt::dense_set<std::type_index>>>
        query_types;
    entt::dense_set<std::type_index> resource_types;
    entt::dense_set<std::type_index> resource_const;
    entt::dense_set<std::type_index> event_read_types;
    entt::dense_set<std::type_index> event_write_types;

    bool conflict_with(const SystemParamInfo& other) const {
        // any system with world is not thread safe, since you can get any
        // allowed param type from a single world.
        if (has_world || other.has_world) {
            return true;
        }
        // use command and query at the same time is now always thread safe
        // if (has_command && (other.has_command || other.has_query)) {
        //     return true;
        // }
        // if (other.has_command && (has_command || has_query)) {
        //     return true;
        // }
        // if two systems use command at the same time, it is not thread safe
        if (has_command && other.has_command) {
            return true;
        }
        // check if queries conflict
        static auto query_conflict =
            [](const std::tuple<
                   entt::dense_set<std::type_index>,
                   entt::dense_set<std::type_index>,
                   entt::dense_set<std::type_index>>& query,
               const std::tuple<
                   entt::dense_set<std::type_index>,
                   entt::dense_set<std::type_index>,
                   entt::dense_set<std::type_index>>& other_query) -> bool {
            auto&& [get_a, with_a, without_a] = query;
            auto&& [get_b, with_b, without_b] = other_query;
            for (auto& type : without_a) {
                if (get_b.contains(type) || with_b.contains(type)) return false;
            }
            for (auto& type : without_b) {
                if (get_a.contains(type) || with_a.contains(type)) return false;
            }
            for (auto& type : get_a) {
                if (get_b.contains(type) || with_b.contains(type)) return true;
            }
            for (auto& type : get_b) {
                if (get_a.contains(type) || with_a.contains(type)) return true;
            }
            return false;
        };
        for (auto& query : query_types) {
            for (auto& other_query : other.query_types) {
                if (query_conflict(query, other_query)) {
                    return true;
                }
            }
        }
        // check if resources conflict
        for (auto& res : resource_types) {
            if (other.resource_types.contains(res) ||
                other.resource_const.contains(res)) {
                return true;
            }
        }
        for (auto& res : other.resource_types) {
            if (resource_types.contains(res) || resource_const.contains(res)) {
                return true;
            }
        }
        // check if events conflict
        // currently event read and event write can all modify the
        // queue, but in future maybe we will replace it by a
        // thread safe queue
        for (auto& event : event_read_types) {
            if (other.event_read_types.contains(event) ||
                other.event_write_types.contains(event)) {
                return true;
            }
        }
        for (auto& event : other.event_read_types) {
            if (event_read_types.contains(event) ||
                event_write_types.contains(event)) {
                return true;
            }
        }
        for (auto& event : event_write_types) {
            if (other.event_read_types.contains(event) ||
                other.event_write_types.contains(event)) {
                return true;
            }
        }
        for (auto& event : other.event_write_types) {
            if (event_read_types.contains(event) ||
                event_write_types.contains(event)) {
                return true;
            }
        }
        return false;
    }
};

template <typename T>
struct SystemParamInfoWrite;

template <>
struct SystemParamInfoWrite<World&> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.has_world = true;
    }
};

template <typename T>
struct SystemParamInfoWrite<Local<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {}
};

template <typename T>
struct SystemParamInfoWrite<Res<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.resource_const.emplace(typeid(T));
    }
};

template <typename T>
struct SystemParamInfoWrite<ResMut<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        if constexpr (std::is_const_v<T> || external_thread_safe_v<T>) {
            dst.resource_const.emplace(typeid(T));
        } else {
            dst.resource_types.emplace(typeid(T));
        }
    }
};

template <typename T>
struct QueryTypeDecay {
    using type = T;
};
template <typename T>
struct QueryTypeDecay<Has<T>> {
    using type = T;
};
template <typename T>
struct QueryTypeDecay<Opt<T>> {
    using type = T;
};
template <>
struct QueryTypeDecay<Entity> {
    using type = const Entity;
};

template <typename G, typename W, typename WO>
struct SystemParamInfoWrite<Query<G, W, WO>> {
    template <typename T>
    struct query_info_write {
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {}
    };
    template <typename... Args>
    struct query_info_write<With<Args...>> {
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {
            auto&& [mutable_types, const_types, exclude_types] =
                dst.query_types.back();
            (const_types.emplace(typeid(std::decay_t<Args>)), ...);
        }
    };
    template <typename... Args>
    struct query_info_write<Without<Args...>> {
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {
            auto&& [mutable_types, const_types, exclude_types] =
                dst.query_types.back();
            (exclude_types.emplace(typeid(std::decay_t<Args>)), ...);
        }
    };
    template <typename... Args>
    struct query_info_write<Get<Args...>> {
        template <typename T>
        struct write_single {
            static void add(SystemParamInfo& src, SystemParamInfo& dst) {
                auto&& [mutable_types, const_types, exclude_types] =
                    dst.query_types.back();
                if constexpr (std::is_const_v<T> || external_thread_safe_v<T>) {
                    const_types.emplace(typeid(std::decay_t<T>));
                } else {
                    mutable_types.emplace(typeid(std::decay_t<T>));
                }
            }
        };
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {
            (write_single<typename QueryTypeDecay<Args>::type>::add(src, dst),
             ...);
        }
    };
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.query_types.emplace_back(
            entt::dense_set<std::type_index>{},
            entt::dense_set<std::type_index>{},
            entt::dense_set<std::type_index>{}
        );
        query_info_write<G>::add(src, dst);
        query_info_write<W>::add(src, dst);
        query_info_write<WO>::add(src, dst);
    }
};

template <>
struct SystemParamInfoWrite<Commands> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.has_command = true;
    }
};

template <typename T>
struct SystemParamInfoWrite<Extract<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        SystemParamInfoWrite<T>::add(dst, src);
    }
};

template <typename... Args>
struct SystemParamInfoWrite<std::tuple<Args...>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        (SystemParamInfoWrite<Args>::add(src, dst), ...);
    }
};

template <typename Ret>
struct BasicSystem {
   protected:
    LocalData m_locals;
    std::function<Ret(World*, World*, BasicSystem*)> m_func;
    double factor;
    double avg_time;  // in milliseconds
    SystemParamInfo system_param_src, system_param_dst;

    entt::dense_set<const BasicSystem*> m_contrary_to;
    entt::dense_set<const BasicSystem*> m_not_contrary_to;

   public:
    bool contrary_to(BasicSystem* other) {
        if (m_contrary_to.find(other) != m_contrary_to.end()) return true;
        if (m_not_contrary_to.find(other) != m_not_contrary_to.end())
            return false;
        if (system_param_src.conflict_with(other->system_param_src) ||
            system_param_dst.conflict_with(other->system_param_dst)) {
            if (m_contrary_to.size() < 4096) m_contrary_to.emplace(other);
            return true;
        } else {
            if (m_not_contrary_to.size() < 4096)
                m_not_contrary_to.emplace(other);
            return false;
        }
    }
    const double get_avg_time() const { return avg_time; }
    template <typename T>
    BasicSystem(T&& func) : BasicSystem(std::function(std::forward<T>(func))) {}
    template <typename... Args>
    // requires(ValidSystemParam<param_decay_t<Args>> && ...)
    BasicSystem(std::function<Ret(Args...)> func)
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              ParamResolver<std::tuple<Args...>> param_resolver(
                  src, dst, &sys->m_locals
              );
              auto resolved = param_resolver.resolve();
              return std::apply(func, resolved);
          }),
          factor(0.1),
          avg_time(1) {
        SystemParamInfoWrite<typename ParamResolve<std::tuple<std::decay_t<
            Args>...>>::root_params>::add(system_param_src, system_param_dst);
    }
    template <typename... Args>
    // requires(ValidSystemParam<param_decay_t<Args>> && ...)
    BasicSystem(Ret (*func)(Args...))
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              ParamResolver<std::tuple<Args...>> param_resolver(
                  src, dst, &sys->m_locals
              );
              auto resolved = param_resolver.resolve();
              return std::apply(func, resolved);
          }),
          factor(0.1),
          avg_time(1) {
        SystemParamInfoWrite<typename ParamResolve<std::tuple<std::decay_t<
            Args>...>>::root_params>::add(system_param_src, system_param_dst);
    }
    BasicSystem(const BasicSystem& other)            = default;
    BasicSystem(BasicSystem&& other)                 = default;
    BasicSystem& operator=(const BasicSystem& other) = default;
    BasicSystem& operator=(BasicSystem&& other)      = default;
    Ret run(World* src, World* dst) {
        auto start = std::chrono::high_resolution_clock::now();
        if constexpr (std::is_same_v<Ret, void>) {
            m_func(src, dst, this);
            auto end = std::chrono::high_resolution_clock::now();
            auto delta =
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - start
                )
                    .count() /
                1000000.0;
            avg_time = delta * 0.1 + avg_time * 0.9;
        } else {
            auto&& ret = m_func(src, dst, this);
            auto end   = std::chrono::high_resolution_clock::now();
            auto delta =
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - start
                )
                    .count() /
                1000000.0;
            avg_time = delta * 0.1 + avg_time * 0.9;
            return ret;
        }
    }
};
}  // namespace epix::app