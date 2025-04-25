#pragma once

// ----THIRD PARTY INCLUDES----
#include <epix/utils/core.h>
#include <spdlog/spdlog.h>

#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include <BS_thread_pool.hpp>
#include <entt/container/dense_set.hpp>
#include <entt/entity/registry.hpp>

// ----STANDARD LIBRARY INCLUDES----
#include <chrono>
#include <concepts>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>
// ----STANDARD LIBRARY INCLUDES----

#include "app/params.h"
#include "app/system.h"
#include "app/tool.h"
#include "app/world.h"

// ----EPIX API----
#include "epix/common.h"
// ----EPIX API----

#if defined(EPIX_ENABLE_TRACY) && defined(NDEBUG)
#include <tracy/Tracy.hpp>
#else
#undef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace epix::app {
struct World;

struct FuncIndex;
struct Entity;
struct Children;

template <typename Ret>
struct BasicSystem;

struct SystemSet;
struct System;
struct SystemAddInfo;

template <typename T>
struct EventReader;
template <typename T>
struct EventWriter;
template <typename T>
struct State;
template <typename T>
struct NextState;

struct Schedule;
struct ScheduleId;
struct ScheduleInfo;

struct App;

using thread_pool = BS::thread_pool<BS::tp::priority>;
}  // namespace epix::app

template <>
struct std::hash<epix::app::Entity> {
    EPIX_API size_t operator()(const epix::app::Entity& entity) const;
};

namespace epix::app {
struct FuncIndex : public Label {
    template <typename T, typename... Args>
    FuncIndex(T (*func)(Args...)) : Label(typeid(func), (size_t)func) {}
    FuncIndex() : Label() {}
    using Label::operator==;
    using Label::operator!=;
};

struct Children {
    dense_set<Entity> children;
};
struct Parent {
    Entity id;
};

template <typename T>
struct State {
    friend class app::App;

   protected:
    T m_state;
    bool just_created = true;

   public:
    State() : m_state() {}
    State(const T& state) : m_state(state) {}
    bool is_just_created() const { return just_created; }

    bool is_state(const T& state) const { return m_state == state; }
    bool is_state(const NextState<T>& state) const {
        return m_state == state.m_state;
    }
};
template <typename T>
struct NextState : public State<T> {
   public:
    NextState() : State<T>() {}
    NextState(const T& state) : State<T>(state) {}

    void set_state(const T& state) { State<T>::m_state = state; }
};
template <typename T>
struct Events {
   private:
    std::deque<std::pair<T, uint32_t>> m_events;  // event lifetime pair
    uint32_t m_head;
    uint32_t m_tail;  // m_tail - m_head should be equal to m_events.size()

   public:
    Events() : m_head(0), m_tail(0) {}
    Events(const Events&) = delete;
    Events(Events&& other) {
        m_events = std::move(other.m_events);
        m_head   = other.m_head;
        m_tail   = other.m_tail;
    }
    Events& operator=(const Events&) = delete;
    Events& operator=(Events&& other) {
        m_events = std::move(other.m_events);
        m_head   = other.m_head;
        m_tail   = other.m_tail;
        return *this;
    }

    void push(const T& event) {
        m_events.emplace_back(event, 1);
        m_tail++;
    }
    void push(T&& event) {
        m_events.emplace_back(std::move(event), 1);
        m_tail++;
    }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events.emplace_back(T(std::forward<Args>(args)...), 1);
        m_tail++;
    }
    void update() {
        while (!m_events.empty() && m_events.front().second == 0) {
            m_events.pop_front();
            m_head++;
        }
        for (auto& event : m_events) {
            if (event.second > 0) {
                event.second--;
            }
        }
    }
    void clear() {
        m_events.clear();
        m_head = m_tail;
    }
    bool empty() const { return m_events.empty(); }
    size_t size() const { return m_events.size(); }
    uint32_t head() const { return m_head; }
    uint32_t tail() const { return m_tail; }
    T* get(uint32_t index) {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head].first;
        }
        return nullptr;
    }
    const T* get(uint32_t index) const {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head].first;
        }
        return nullptr;
    }
};
template <typename T>
struct EventPointer {
    uint32_t index = 0;
};
template <typename T>
struct EventReader {
    struct iterator {
        uint32_t index;
        const Events<T>* events;
        iterator(uint32_t index, const Events<T>* events)
            : index(index), events(events) {}
        iterator& operator++() {
            index++;
            return *this;
        }
        bool operator==(const iterator& rhs) const {
            return index == rhs.index;
        }
        bool operator!=(const iterator& rhs) const {
            return index != rhs.index;
        }
        const T& operator*() {
            auto event = events->get(index);
            return *event;
        }
    };
    struct iterator_index {
        uint32_t index;
        const Events<T>* events;
        iterator_index(uint32_t index, const Events<T>* events)
            : index(index), events(events) {}
        iterator_index& operator++() {
            index++;
            return *this;
        }
        bool operator==(const iterator_index& rhs) const {
            return index == rhs.index;
        }
        bool operator!=(const iterator_index& rhs) const {
            return index != rhs.index;
        }
        std::pair<uint32_t, const T&> operator*() {
            auto event = events->get(index);
            return {index, *event};
        }
    };
    struct iterable {
       private:
        const uint32_t m_read_begin;
        const Events<T>* m_events;
        const iterator m_begin;
        const iterator m_end;

       public:
        iterable(uint32_t read_begin, const Events<T>* events)
            : m_read_begin(read_begin),
              m_events(events),
              m_begin(read_begin, events),
              m_end(events->tail(), events) {}
        iterator begin() { return m_begin; }
        iterator end() { return m_end; }
    };
    struct iterable_index {
       private:
        const uint32_t m_read_begin;
        const Events<T>* m_events;
        const iterator_index m_begin;
        const iterator_index m_end;

       public:
        iterable_index(uint32_t read_begin, const Events<T>* events)
            : m_read_begin(read_begin),
              m_events(events),
              m_begin(read_begin, events),
              m_end(events->tail(), events) {}
        iterator_index begin() { return m_begin; }
        iterator_index end() { return m_end; }
    };

   private:
    Local<EventPointer<T>> m_pointer;
    Res<Events<T>> m_events;
    EventReader(Local<EventPointer<T>> pointer, Res<Events<T>> events)
        : m_pointer(pointer), m_events(events) {}

   public:
    static EventReader<T> from_system_param(
        Local<EventPointer<T>> pointer, Res<Events<T>> events
    ) {
        pointer->index = std::max(pointer->index, events->head());
        pointer->index = std::min(pointer->index, events->tail());
        return EventReader<T>(pointer, events);
    }

    /**
     * @brief Iterating through events this reader has not yet read.
     *
     * @return `iterable` object that can be used to iterate through events.
     */
    auto read() {
        iterable iter(m_pointer->index, m_events.get());
        m_pointer->index = m_events->tail();
        return iter;
    }
    auto read_with_index() {
        iterable_index iter(m_pointer->index, m_events.get());
        m_pointer->index = m_events->tail();
        return iter;
    }
    /**
     * @brief Get the remaining events this reader has not yet read.
     *
     * @return `size_t` number of events remaining.
     */
    size_t size() const { return m_events->tail() - m_pointer->index; }
    /**
     * @brief Read the next event.
     *
     * @return `T*` pointer to the next event, or `nullptr` if there are no more
     */
    T* read_one() {
        auto event = m_events->get(m_pointer->index);
        if (event) {
            m_pointer->index++;
            return event;
        } else {
            return nullptr;
        }
    }
    /**
     * @brief Read the next event and its index.
     *
     * @return `std::pair<uint32_t, T*>` pair of the index and pointer to the
     * next event, or {current_ptr, nullptr} if there are no more
     */
    std::pair<uint32_t, T*> read_one_index() {
        auto pair =
            std::make_pair(m_pointer->index, m_events->get(m_pointer->index));
        if (pair.second) {
            m_pointer->index++;
        }
        return pair;
    }
    bool empty() const { return m_pointer->index == m_events->tail(); }
};
template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>> m_events;
    EventWriter(ResMut<Events<T>> events) : m_events(events) {}

   public:
    static EventWriter<T> from_system_param(ResMut<Events<T>> events) {
        return EventWriter<T>(events);
    }

    void write(const T& event) { m_events->push(event); }
    void write(T&& event) { m_events->push(std::move(event)); }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
    }

    friend struct App;
};

struct SystemSet {
    std::type_index type;
    size_t value;

    template <typename T>
        requires std::is_enum_v<T>
    SystemSet(T value)
        : type(typeid(std::decay_t<T>)), value(static_cast<size_t>(value)) {}

    EPIX_API bool operator==(const SystemSet& other) const;
    EPIX_API bool operator!=(const SystemSet& other) const;
};

struct SystemAddInfo {
    struct each_t {
        std::string name;
        FuncIndex index;
        std::unique_ptr<BasicSystem<void>> system;
        std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

        std::vector<SystemSet> m_in_sets;
        std::string m_worker = "default";
        dense_set<FuncIndex> m_ptr_prevs;
        dense_set<FuncIndex> m_ptr_nexts;

        EPIX_API each_t(
            const std::string& name,
            FuncIndex index,
            std::unique_ptr<BasicSystem<void>>&& system
        );
        EPIX_API each_t(each_t&& other);
        EPIX_API each_t& operator=(each_t&& other);
    };
    std::vector<each_t> m_systems;

    SystemAddInfo()                                      = default;
    SystemAddInfo(const SystemAddInfo& other)            = delete;
    SystemAddInfo(SystemAddInfo&& other)                 = default;
    SystemAddInfo& operator=(const SystemAddInfo& other) = delete;
    SystemAddInfo& operator=(SystemAddInfo&& other)      = default;

    EPIX_API SystemAddInfo& chain();

    template <typename T, typename... Args>
    SystemAddInfo& before(T (*func)(Args...)) {
        for (auto& each : m_systems) {
            each.m_ptr_nexts.emplace(FuncIndex(func));
        }
        return *this;
    }
    template <typename T, typename... Args>
    SystemAddInfo& after(T (*func)(Args...)) {
        for (auto& each : m_systems) {
            each.m_ptr_prevs.emplace(FuncIndex(func));
        }
        return *this;
    }
    template <typename T>
        requires std::is_enum_v<T>
    SystemAddInfo& in_set(T t) {
        for (auto& each : m_systems) {
            each.m_in_sets.emplace_back(SystemSet(t));
        }
        return *this;
    }
    EPIX_API SystemAddInfo& worker(const std::string& worker);
    template <typename... Args>
    SystemAddInfo& run_if(const std::function<bool(Args...)>& func) {
        for (auto& each : m_systems) {
            each.conditions.emplace_back(
                std::make_unique<BasicSystem<bool>>(func)
            );
        }
        return *this;
    }
    template <typename... Args>
    SystemAddInfo& run_if(std::function<bool(Args...)>&& func) {
        for (auto& each : m_systems) {
            each.conditions.emplace_back(
                std::make_unique<BasicSystem<bool>>(func)
            );
        }
        return *this;
    }
    template <typename... Args>
    SystemAddInfo& run_if(bool (*func)(Args...)) {
        for (auto& each : m_systems) {
            each.conditions.emplace_back(
                std::make_unique<BasicSystem<bool>>(func)
            );
        }
        return *this;
    }
    template <typename T>
        requires requires(T t) {
            { std::function(t) };
        }
    SystemAddInfo& run_if(T&& func) {
        run_if(std::function(func));
        return *this;
    }
    template <typename T>
    SystemAddInfo& in_state(T state) {
        run_if([state](Res<State<T>> cur) { return cur->is_state(state); });
        return *this;
    };
    EPIX_API SystemAddInfo& set_label(const std::string& label);
    EPIX_API SystemAddInfo& set_label(uint32_t index, const std::string& label);
    EPIX_API SystemAddInfo& set_labels(index::ArrayProxy<std::string> labels);
    EPIX_API SystemAddInfo& set_labels(index::ArrayProxy<const char*> labels);
};

struct System {
    EPIX_API System(
        const std::string& label,
        FuncIndex index,
        std::unique_ptr<BasicSystem<void>>&& system
    );
    System(const System& other)            = delete;
    System(System&& other)                 = delete;
    System& operator=(const System& other) = delete;
    System& operator=(System&& other)      = delete;

    EPIX_API bool run(World* src, World* dst, bool enable_tracy);
    EPIX_API void clear_tmp();
    EPIX_API double reach_time();

    std::string label;
    FuncIndex index;
    std::unique_ptr<BasicSystem<void>> system;
    std::string worker;
    std::vector<SystemSet> sets;
    std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

    // set when building
    dense_set<std::weak_ptr<System>> m_prevs;
    dense_set<std::weak_ptr<System>> m_nexts;

    // set when baking
    dense_set<std::weak_ptr<System>> m_tmp_prevs;
    dense_set<std::weak_ptr<System>> m_tmp_nexts;

    // set when creationg
    dense_set<FuncIndex> m_ptr_prevs;
    dense_set<FuncIndex> m_ptr_nexts;

    // used when baking
    std::optional<double> m_reach_time;

    // used when running
    size_t m_prev_count;
    size_t m_next_count;
};
using SetMap = entt::dense_map<std::type_index, std::vector<SystemSet>>;
struct ScheduleId : Label {
    template <typename T>
    ScheduleId(T value) : Label(value) {}
    using Label::operator==;
    using Label::operator!=;
};
struct Executor {
   private:
    dense_map<std::string, std::shared_ptr<BS::thread_pool<BS::tp::priority>>>
        m_pools;

   public:
    EPIX_API void add(const std::string& name, size_t count);
    EPIX_API std::shared_ptr<BS::thread_pool<BS::tp::priority>> get(
        const std::string& name
    );
};
enum ScheduleExecutionType {
    SingleThread,
    MultiThread,
};
struct Schedule {
   private:
    SetMap m_sets;
    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<Executor> m_executor;
    ScheduleId m_id;
    dense_map<FuncIndex, std::shared_ptr<System>> m_systems;
    bool m_run_once;

    std::type_index m_src_world;
    std::type_index m_dst_world;

    // set when building
    dense_set<std::weak_ptr<Schedule>> m_prev_schedules;
    dense_set<std::weak_ptr<Schedule>> m_next_schedules;

    // set when baking
    dense_set<std::weak_ptr<Schedule>> m_tmp_prevs;
    dense_set<std::weak_ptr<Schedule>> m_tmp_nexts;

    // set when creation
    dense_set<ScheduleId> m_prev_ids;
    dense_set<ScheduleId> m_next_ids;

    // used when running
    std::shared_ptr<epix::utils::async::ConQueue<std::shared_ptr<System>>>
        m_finishes;
    size_t m_prev_count = 0;

    // cached ops for runtime system adding and removal
    std::vector<FuncIndex> m_removes;
    std::vector<SystemAddInfo> m_adds;
    std::vector<std::pair<uint32_t, uint32_t>> m_cached_ops;
    std::unique_ptr<std::mutex> m_op_mutex;
    bool m_running = false;

    // used when baking
    double m_avg_time  = 1.0;
    double m_last_time = 0.0;
    std::optional<double> m_reach_time;

    EPIX_API Schedule& set_logger(const std::shared_ptr<spdlog::logger>& logger
    );

   public:
    EPIX_API Schedule(ScheduleId id);
    Schedule(Schedule&& other)            = default;
    Schedule& operator=(Schedule&& other) = default;

    EPIX_API Schedule& set_executor(const std::shared_ptr<Executor>& executor);
    template <typename T, typename... Args>
    Schedule& after(T scheduleId, Args... others) {
        m_prev_ids.emplace(scheduleId);
        (m_prev_ids.emplace(others), ...);
        return *this;
    }
    template <typename T, typename... Args>
    Schedule& before(T scheduleId, Args... others) {
        m_next_ids.emplace(scheduleId);
        (m_next_ids.emplace(others), ...);
        return *this;
    }
    template <typename T, typename... Args>
    Schedule& configure_sets(T set, Args... others) {
        m_sets.emplace(
            std::type_index(typeid(std::decay_t<T>)),
            std::vector<SystemSet>{
                SystemSet(std::forward<T>(set)),
                SystemSet(std::forward<Args>(others))...
            }
        );
        return *this;
    }
    template <typename T>
    Schedule& set_src_world() {
        m_src_world = typeid(std::decay_t<T>);
        return *this;
    }
    template <typename T>
    Schedule& set_dst_world() {
        m_dst_world = typeid(std::decay_t<T>);
        return *this;
    }
    EPIX_API Schedule& add_system(SystemAddInfo&& info);
    EPIX_API Schedule& remove_system(FuncIndex index);
    EPIX_API void build();
    EPIX_API void bake();
    EPIX_API void run(World* src, World* dst, bool enable_tracy);
    EPIX_API void run(
        std::shared_ptr<System> system,
        World* src,
        World* dst,
        bool enable_tracy
    );
    EPIX_API Schedule& run_once(bool once = true);
    EPIX_API double get_avg_time() const;
    EPIX_API void clear_tmp();
    EPIX_API double reach_time();

    friend struct App;
};
struct ScheduleInfo : public ScheduleId {
    using ScheduleId::ScheduleId;
    std::vector<std::function<void(SystemAddInfo&)>> transforms;
};

inline struct PreStartupT {
} PreStartup;
inline struct StartupT {
} Startup;
inline struct PostStartupT {
} PostStartup;
inline struct FirstT {
} First;
inline struct PreUpdateT {
} PreUpdate;
inline struct UpdateT {
} Update;
inline struct PostUpdateT {
} PostUpdate;
inline struct LastT {
} Last;
inline struct PrepareT {
} Prepare;
inline struct PreRenderT {
} PreRender;
inline struct RenderT {
} Render;
inline struct PostRenderT {
} PostRender;
inline struct PreExitT {
} PreExit;
inline struct ExitT {
} Exit;
inline struct PostExitT {
} PostExit;
inline struct StateTransitionT {
} StateTransition;
enum StateTransitionSet {
    Transit,
    Callback,
};
inline struct PreExtractT {
} PreExtract;
inline struct ExtractionT {
} Extraction;
inline struct PostExtractT {
} PostExtract;
inline struct OnEnterT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemAddInfo& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return (cur->is_state(state) && cur->is_just_created()) ||
                       (!cur->is_state(state) && next->is_state(state));
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnEnter;
inline struct OnExitT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemAddInfo& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return cur->is_state(state) && !next->is_state(state);
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnExit;
inline struct OnChangeT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemAddInfo& info) {
            info.run_if([](Res<State<T>> cur, Res<NextState<T>> next) {
                return !cur->is_state(next->m_state);
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;

struct MainWorld {};
struct RenderWorld {};

struct ExitControl {};

struct AppExit {};

template <typename T>
    requires std::same_as<SystemAddInfo, std::decay_t<T>>
void info_append(SystemAddInfo& info, T&& func) {
    SystemAddInfo&& info2 = std::move(func);
    std::move(
        info2.m_systems.begin(), info2.m_systems.end(),
        std::back_inserter(info.m_systems)
    );
}

template <typename Func>
    requires requires(Func func) {
        { std::function(func) };
    } || std::is_function_v<std::remove_pointer_t<std::decay_t<Func>>>
void info_append(SystemAddInfo& info, Func&& func) {
    if constexpr (std::is_function_v<
                      std::remove_pointer_t<std::decay_t<Func>>>) {
        info.m_systems.emplace_back(
            std::format("system:{:#016x}", (size_t)func), FuncIndex(func),
            std::make_unique<BasicSystem<void>>(func)
        );
    } else {
        auto ptr = std::make_unique<BasicSystem<void>>(func);
        FuncIndex index;
        index.set_index((size_t)ptr.get());
        info.m_systems.emplace_back(
            std::format("system:{:#016x}", (size_t)ptr.get()), index,
            std::move(ptr)
        );
    }
}

template <typename... Funcs>
SystemAddInfo into(Funcs&&... funcs) {
    SystemAddInfo info;
    info.m_systems.reserve(sizeof...(funcs));
    (info_append(info, std::forward<Funcs>(funcs)), ...);
    return std::move(info);
}

template <>
EPIX_API SystemAddInfo into(SystemAddInfo&& info);

struct Plugin {
    virtual void build(App& app) = 0;
};

struct AppCreateInfo {
    uint32_t control_threads                                     = 2;
    std::vector<std::pair<std::string, uint32_t>> worker_threads = {
        {"default", 4},
        {"single", 1},
    };
    bool enable_loop                       = false;
    bool enable_tracy                      = true;
    bool enable_frame_mark                 = true;
    std::shared_ptr<spdlog::logger> logger = nullptr;
};

inline struct StartGraphT {
} StartGraph;
inline struct LoopGraphT {
} LoopGraph;
inline struct ExitGraphT {
} ExitGraph;
struct GraphId : public std::type_index {
    template <typename T>
    GraphId(T value) : std::type_index(typeid(std::decay_t<T>)) {}
    GraphId(const GraphId& other)            = default;
    GraphId(GraphId&& other)                 = default;
    GraphId& operator=(const GraphId& other) = default;
    GraphId& operator=(GraphId&& other)      = default;
};

struct AppProfile {
    /// in milliseconds
    double frame_time = 0.0;
    /// frames per second
    double fps = 0.0;
};

struct ScheduleProfiles {
    struct ScheduleProfile {
        double time_last = 0.0;
        double time_avg  = 0.0;
    };

   private:
    dense_map<ScheduleId, ScheduleProfile> m_profiles;

   public:
    EPIX_API ScheduleProfile& profile(ScheduleId id);
    EPIX_API const ScheduleProfile& profile(ScheduleId id) const;
    EPIX_API ScheduleProfile* get_profile(ScheduleId id);
    EPIX_API const ScheduleProfile* get_profile(ScheduleId id) const;
    EPIX_API void for_each(
        const std::function<void(ScheduleId, ScheduleProfile&)>& func
    );
    EPIX_API void for_each(
        const std::function<void(ScheduleId, const ScheduleProfile&)>& func
    ) const;

    friend struct Schedule;
};

/**
 * @brief This is a struct that contains the setting data of the app.
 * This is not designed to be mutable, but maybe in the future we will
 * add some mutable data to it.
 */
struct AppInfo {
    bool enable_loop;
    bool enable_tracy;
    bool tracy_frame_mark;
};

struct AppSystems {
   private:
    App& app;

   public:
    AppSystems(App& app) : app(app) {}

    template <typename... Funcs>
    AppSystems& add_system(ScheduleInfo id, Funcs&&... funcs);
    template <typename... Args>
    AppSystems& remove_system(ScheduleId id, Args&&... args);
};

struct App {
    struct ScheduleGraph {
        dense_map<ScheduleId, std::shared_ptr<Schedule>> m_schedules;
        epix::utils::async::ConQueue<std::shared_ptr<Schedule>> m_finishes;
    };

   private:
    // plugin to build
    std::vector<std::pair<std::type_index, std::shared_ptr<Plugin>>> m_plugins;
    dense_set<std::type_index> m_built_plugins;

    // world data
    dense_map<std::type_index, std::unique_ptr<World>> m_worlds;

    // graph data
    dense_map<std::type_index, std::unique_ptr<ScheduleGraph>> m_graphs;
    dense_map<ScheduleId, std::type_index> m_graph_ids;

    // control and worker pools
    std::unique_ptr<BS::thread_pool<BS::tp::priority>> m_pool;
    std::shared_ptr<Executor> m_executor;

    // settings
    std::unique_ptr<bool> m_enable_loop;
    std::unique_ptr<bool> m_enable_tracy;
    std::unique_ptr<bool> m_tracy_frame_mark;

    // logger
    std::shared_ptr<spdlog::logger> m_logger;

    EPIX_API App(const AppCreateInfo& info);

   public:
    EPIX_API static App create(const AppCreateInfo& info = {});
    EPIX_API static App create2(const AppCreateInfo& info = {});

    EPIX_API App* operator->();
    EPIX_API App& enable_loop();
    EPIX_API App& disable_loop();
    EPIX_API App& enable_tracy();
    EPIX_API App& disable_tracy();
    EPIX_API App& enable_frame_mark();
    EPIX_API App& disable_frame_mark();
    EPIX_API App& set_log_level(spdlog::level::level_enum level);

    EPIX_API App& add_schedule(GraphId, Schedule&& schedule);
    EPIX_API App& add_schedule(GraphId, Schedule& schedule);

    template <typename... Schs>
        requires(sizeof...(Schs) != 1)
    App& add_schedule(GraphId id, Schs&&... schs) {
        (add_schedule(id, std::forward<Schs>(schs)), ...);
        return *this;
    }

    EPIX_API App& set_logger(const std::shared_ptr<spdlog::logger>& logger);
    EPIX_API std::shared_ptr<spdlog::logger> logger() const;

    template <typename T>
    App& add_world() {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (!m_worlds.contains(id)) {
            m_worlds.emplace(id, std::make_unique<World>());
        }
        return *this;
    };
    template <typename T>
    World& world() {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (auto it = m_worlds.find(id); it != m_worlds.end()) {
            return *it->second;
        }
        m_logger->error(
            "World {} not found. Please add it first.", typeid(T).name()
        );
        throw std::runtime_error("World not found.");
    };
    template <typename T>
    World* get_world() {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (auto it = m_worlds.find(id); it != m_worlds.end()) {
            return it->second.get();
        }
        return nullptr;
    };

    template <typename... Args>
    App& configure_sets(Args... sets) {
        for (auto&& [id, graph] : m_graphs) {
            for (auto&& [type, schedules] : graph->m_schedules) {
                schedules->configure_sets(sets...);
            }
        }
        return *this;
    }

    template <typename... Funcs>
    App& add_system(ScheduleInfo info, Funcs&&... funcs) {
        ScheduleId& id = info;
        if (!m_graph_ids.contains(id)) {
            m_logger->warn(
                "Schedule {} not found. Ignoring add_system.", id.name()
            );
            return *this;
        }
        auto&& graph        = m_graphs.at(m_graph_ids.at(id));
        auto&& schedule     = graph->m_schedules[id];
        auto&& system_infos = std::array<SystemAddInfo, sizeof...(Funcs)>{
            into(std::forward<Funcs>(funcs))...
        };
        for (auto&& system_info : system_infos) {
            for (auto&& transform : info.transforms) {
                transform(system_info);
            }
        }
        for (auto&& system_info : system_infos) {
            schedule->add_system(std::move(system_info));
        }
        return *this;
    };
    template <typename... Args>
    App& remove_system(ScheduleId id, FuncIndex index, Args&&... args) {
        if (!m_graph_ids.contains(id)) {
            m_logger->warn(
                "Schedule {} not found. Ignoring remove_system.", id.name()
            );
            return *this;
        }
        auto&& graph    = m_graphs.at(m_graph_ids.at(id));
        auto&& schedule = graph->m_schedules[id];
        schedule->remove_system(index);
        if constexpr (sizeof...(Args)) {
            remove_system(id, args...);
        }
        return *this;
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            ) == m_plugins.end()) {
            m_plugins.emplace_back(
                id, std::make_shared<std::decay_t<T>>(std::forward<T>(plugin))
            );
        }
        return *this;
    };

    template <typename T>
    T& plugin() {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (auto it = std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            );
            it != m_plugins.end()) {
            return *std::static_pointer_cast<T>(it->second).get();
        }
        throw std::runtime_error("Plugin not found.");
    };
    template <typename T>
    std::shared_ptr<T> get_plugin() {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (auto it = std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            );
            it != m_plugins.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    };

    template <typename T, typename... Args>
    App& emplace_resource(Args&&... args) {
        auto&& w = world<MainWorld>();
        w.emplace_resource<T>(std::forward<Args>(args)...);
        return *this;
    };
    template <typename T>
    App& insert_resource(T&& resource) {
        auto&& w = world<MainWorld>();
        w.insert_resource(std::forward<T>(resource));
        return *this;
    };
    template <typename T>
    App& init_resource() {
        auto&& w = world<MainWorld>();
        w.init_resource<T>();
        return *this;
    };
    template <typename T>
    App& add_resource(T* resource) {
        auto&& w = world<MainWorld>();
        w.add_resource<T>(resource);
        return *this;
    };
    template <typename T>
    App& remove_resource() {
        auto&& w = world<MainWorld>();
        w.remove_resource<T>();
        return *this;
    };
    template <typename T>
    App& insert_state(T&& state) {
        auto&& w = world<MainWorld>();
        w.insert_resource<State<T>>(State<T>(std::forward<T>(state)));
        w.insert_resource<NextState<T>>(NextState<T>(std::forward<T>(state)));
        SystemAddInfo info;
        info.m_systems.emplace_back(
            std::format("update State<{}>", typeid(T).name()), FuncIndex(),
            std::make_unique<BasicSystem<void>>([](ResMut<State<T>> state,
                                                   Res<NextState<T>> next) {
                state->m_state = next->m_state;
            })
        );
        add_system(
            StateTransition, std::move(info.in_set(StateTransitionSet::Transit))
        );
        return *this;
    };
    template <typename T>
    App& init_state() {
        auto&& w = world<MainWorld>();
        w.init_resource<State<T>>();
        w.init_resource<NextState<T>>();
        SystemAddInfo info;
        info.m_systems.emplace_back(
            std::format("update State<{}>", typeid(T).name()), FuncIndex(),
            std::make_unique<BasicSystem<void>>([](ResMut<State<T>> state,
                                                   Res<NextState<T>> next) {
                state->m_state = next->m_state;
            })
        );
        add_system(
            StateTransition, std::move(info.in_set(StateTransitionSet::Transit))
        );
        return *this;
    };
    template <typename T>
    App& add_event() {
        auto&& w = world<MainWorld>();
        w.init_resource<Events<T>>();
        // SystemAddInfo info;
        // auto func =
        //     std::make_unique<BasicSystem<void>>([](ResMut<Events<T>>
        //     event) {
        //         event->update();
        //     });
        // FuncIndex index;
        // index.func = func.get();
        // info.m_systems.emplace_back(
        //     std::format("update Event<{}>", typeid(T).name()), index,
        //     std::move(func)
        // );
        add_system(
            Last, into([](ResMut<Events<T>> event) { event->update(); }
                  ).set_label(std::format("update Event<{}>", typeid(T).name()))
        );
        return *this;
    };

    EPIX_API void build_plugins();
    EPIX_API void build(ScheduleGraph&);
    EPIX_API void bake(ScheduleGraph&);
    EPIX_API void run(ScheduleGraph&);
    EPIX_API void run();
};
}  // namespace epix::app

namespace epix::utility {
template <typename Resolution = std::chrono::milliseconds>
    requires std::same_as<Resolution, std::chrono::nanoseconds> ||
             std::same_as<Resolution, std::chrono::microseconds> ||
             std::same_as<Resolution, std::chrono::milliseconds> ||
             std::same_as<Resolution, std::chrono::seconds> ||
             std::same_as<Resolution, std::chrono::minutes> ||
             std::same_as<Resolution, std::chrono::hours>
struct time_scope {
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::string name;
    std::function<void(int64_t)> dur_op;
    std::function<void(int64_t, int64_t)> dur_op2;
    std::function<void(int64_t, int64_t, int64_t)> dur_op3;
    time_scope(const std::string& name) : name(name) {
        start = std::chrono::high_resolution_clock::now();
    }
    time_scope(const std::string& name, std::function<void(int64_t)> operation)
        : name(name), dur_op(operation) {
        start = std::chrono::high_resolution_clock::now();
    }
    time_scope(
        const std::string& name, std::function<void(int64_t, int64_t)> operation
    )
        : name(name), dur_op2(operation) {
        start = std::chrono::high_resolution_clock::now();
    }
    time_scope(
        const std::string& name,
        std::function<void(int64_t, int64_t, int64_t)> operation
    )
        : name(name), dur_op3(operation) {
        start = std::chrono::high_resolution_clock::now();
    }
    ~time_scope() {
        auto end        = std::chrono::high_resolution_clock::now();
        auto dur        = std::chrono::duration_cast<Resolution>(end - start);
        auto start_time = std::chrono::time_point_cast<Resolution>(start);
        auto end_time   = std::chrono::time_point_cast<Resolution>(end);

        if (dur_op) dur_op(dur.count());
        if (dur_op2)
            dur_op2(
                start_time.time_since_epoch().count(),
                end_time.time_since_epoch().count()
            );
        if (dur_op3)
            dur_op3(
                start_time.time_since_epoch().count(),
                end_time.time_since_epoch().count(), dur.count()
            );
    }
};
}  // namespace epix::utility

// impl
namespace epix::app {
template <typename T, typename... Args>
void EntityCommands::emplace(Args&&... args) {
    m_world->entity_emplace<T>(m_entity, std::forward<Args>(args)...);
}
template <typename T>
void EntityCommands::insert(T&& obj) {
    m_world->entity_emplace<std::decay_t<T>>(m_entity, std::forward<T>(obj));
}
template <typename... Args>
void EntityCommands::erase() {
    static void (*erase)(World*, Entity) = [](World* world, Entity entity) {
        world->m_registry.erase<Args...>(entity);
    };
    m_command_queue->entity_erase(erase, m_entity);
}
template <typename... Args>
EntityCommands EntityCommands::spawn(Args&&... args) {
    auto entity =
        m_command->spawn(Parent{m_entity}, std::forward<Args>(args)...);
    m_world->m_registry.get_or_emplace<Children>(m_entity).children.emplace(
        entity
    );
    return m_command->entity(entity);
}

template <typename... Args>
EntityCommands Commands::spawn(Args&&... args) noexcept {
    return EntityCommands(
        this, &m_world->m_command_queue,
        m_world->spawn(std::forward<Args>(args)...)
    );
}

template <typename T>
void Commands::insert_resource(T&& res) noexcept {
    m_world->insert_resource(std::forward<T>(res));
}
template <typename T>
void Commands::init_resource() noexcept {
    m_world->init_resource<T>();
}
template <typename T, typename... Args>
void Commands::emplace_resource(Args&&... args) noexcept {
    m_world->emplace_resource<T>(std::forward<Args>(args)...);
}
template <typename T>
void Commands::add_resource(const std::shared_ptr<T>& res) noexcept {
    m_world->add_resource(res);
}
template <typename T>
void Commands::add_resource(T* res) noexcept {
    m_world->add_resource(res);
}
template <typename T>
void Commands::remove_resource() noexcept {
    m_command_queue->remove_resource(typeid(std::decay_t<T>));
}

template <typename... Funcs>
AppSystems& AppSystems::add_system(ScheduleInfo id, Funcs&&... funcs) {
    app.add_system(id, std::forward<Funcs>(funcs)...);
    return *this;
};
template <typename... Args>
AppSystems& AppSystems::remove_system(ScheduleId id, Args&&... args) {
    app.remove_system(id, std::forward<Args>(args)...);
    return *this;
}
}  // namespace epix::app

namespace epix {

// ENTITY PART
using app::App;
using app::AppCreateInfo;
using app::Children;
using app::Entity;
using app::Parent;
using app::Plugin;

// PROFILE
using app::AppProfile;

// EVENTS
using app::AppExit;

// SCHEDULES
using app::Schedule;
using app::ScheduleId;
using app::ScheduleInfo;

using app::Exit;
using app::Extraction;
using app::First;
using app::Last;
using app::OnChange;
using app::OnEnter;
using app::OnExit;
using app::PostExit;
using app::PostExtract;
using app::PostRender;
using app::PostStartup;
using app::PostUpdate;
using app::PreExit;
using app::PreExtract;
using app::Prepare;
using app::PreRender;
using app::PreStartup;
using app::PreUpdate;
using app::Render;
using app::Startup;
using app::Update;

// SYSTEM PARA PART
using app::Commands;
using Command = app::Commands;
using app::EventReader;
using app::EventWriter;
using app::Extract;
using app::Get;
using app::Has;
using app::Local;
using app::NextState;
using app::Opt;
using app::Query;
using app::Res;
using app::ResMut;
using app::State;
using app::With;
using app::Without;

// APP UTILS
using app::AppInfo;
using app::AppSystems;

// OTHER TOOLS
using entt::dense_map;
using entt::dense_set;
using epix::app::into;
using epix::app::thread_pool;
using epix::utility::time_scope;

}  // namespace epix
namespace epix::prelude {
using namespace epix;
}

#define EPIX_INTO(function) epix::app::into(##function).set_label(#function)

#ifndef into2
#define into2(x) EPIX_INTO(##x)
#endif