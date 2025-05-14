#pragma once

#include "profiler.h"
#include "schedule.h"

namespace epix::app {
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
    Local<EventPointer<T>>& m_pointer;
    Res<Events<T>>& m_events;
    EventReader(Local<EventPointer<T>>& pointer, Res<Events<T>>& events)
        : m_pointer(pointer), m_events(events) {}

   public:
    EventReader(const EventReader& other)
        : m_pointer(other.m_pointer), m_events(other.m_events) {}
    EventReader(EventReader&& other)
        : m_pointer(other.m_pointer), m_events(other.m_events) {}
    EventReader& operator=(const EventReader&) = delete;
    EventReader& operator=(EventReader&&)      = delete;

    static EventReader<T> from_param(
        Local<EventPointer<T>>& pointer, Res<Events<T>>& events
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
    const T* read_one() {
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
    std::pair<uint32_t, const T*> read_one_index() {
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
    ResMut<Events<T>>& m_events;
    EventWriter(ResMut<Events<T>>& events) : m_events(events) {}

   public:
    EventWriter(const EventWriter& other) : m_events(other.m_events) {}
    EventWriter(EventWriter&& other) : m_events(other.m_events) {}
    EventWriter& operator=(const EventWriter&) = delete;
    EventWriter& operator=(EventWriter&&)      = delete;

    static EventWriter<T> from_param(ResMut<Events<T>>& events) {
        return EventWriter<T>(events);
    }

    void write(const T& event) { m_events->push(event); }
    void write(T&& event) { m_events->push(std::move(event)); }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
    }
};

template <typename T>
    requires std::is_enum_v<T>
struct State {
    friend struct App;

   protected:
    T m_state;
    bool just_created = true;
    State& operator=(T state) {
        m_state      = state;
        just_created = false;
        return *this;
    }

   public:
    State() : m_state() {}
    State(T state) : m_state(state) {}
    operator T() const { return m_state; }
    bool operator==(T state) const { return m_state == state; }
    bool operator!=(T state) const { return m_state != state; }
    bool is_just_created() const { return just_created; }

    friend struct App;
};
template <typename T>
    requires std::is_enum_v<T>
struct NextState : public State<T> {
   public:
    using State<T>::State;

    operator T() const { return State<T>::m_state; }
    bool operator==(T state) const { return State<T>::m_state == state; }
    bool operator!=(T state) const { return State<T>::m_state != state; }
    NextState& operator=(T state) {
        State<T>::m_state = state;
        return *this;
    }

    void set_state(const T& state) { State<T>::m_state = state; }
};

inline struct MainWorldT {
} MainWorld;
inline struct RenderWorldT {
} RenderWorld;

struct WorldLabel : public Label {
    template <typename T>
    WorldLabel(T t) : Label(t){};
    WorldLabel() noexcept : Label(MainWorld) {}
    // using Label::operator==;
    // using Label::operator!=;
};

struct App;

struct RunGroupError {
    enum class ErrorType {
        ScheduleRemains,
    } type;
    union {
        uint32_t remain_count;
    };
};
struct ScheduleGroup {
    struct ScheduleNode {
        entt::dense_set<ScheduleLabel> depends;
        entt::dense_set<ScheduleLabel> succeeds;
    };
    entt::dense_map<ScheduleLabel, std::unique_ptr<Schedule>> schedules;
    entt::dense_map<ScheduleLabel, WorldLabel> schedule_src;
    entt::dense_map<ScheduleLabel, WorldLabel> schedule_dst;
    entt::dense_map<ScheduleLabel, ScheduleNode> schedule_nodes;
    entt::dense_map<ScheduleLabel, bool> schedule_run_once;

    bool needs_build = true;

    EPIX_API void build() noexcept;
    EPIX_API std::expected<void, RunGroupError> run(App&);
};
struct GroupLabel : public Label {
    template <typename T>
    GroupLabel(T t) : Label(t){};
    // using Label::operator==;
    // using Label::operator!=;
};

struct ScheduleInfo : public ScheduleLabel {
    using ScheduleLabel::ScheduleLabel;
    std::vector<std::function<void(SystemConfig&)>> transforms;
};

inline struct LoopGroupT {
} LoopGroup;

inline struct ExitGroupT {
} ExitGroup;

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
        info.transforms.emplace_back([state](SystemConfig& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return ((cur == state) && cur->is_just_created()) ||
                       ((cur != state) && (next == state));
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
        info.transforms.emplace_back([state](SystemConfig& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return (cur == state) && (next != state);
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
        info.transforms.emplace_back([state](SystemConfig& info) {
            info.run_if([](Res<State<T>> cur, Res<NextState<T>> next) {
                return cur != next;
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;

struct ScheduleConfig {
    ScheduleLabel label;
    std::optional<Schedule> schedule;
    entt::dense_set<ScheduleLabel> depends;
    entt::dense_set<ScheduleLabel> succeeds;
    WorldLabel src_world;
    WorldLabel dst_world;
    bool run_once = false;

    EPIX_API ScheduleConfig(Schedule&& schedule);
    EPIX_API ScheduleConfig(ScheduleLabel label);
    EPIX_API ScheduleConfig& after(ScheduleLabel label);
    EPIX_API ScheduleConfig& before(ScheduleLabel label);
    EPIX_API ScheduleConfig& set_src(WorldLabel label);
    EPIX_API ScheduleConfig& set_dst(WorldLabel label);
    EPIX_API ScheduleConfig& set_run_once();
};

struct Plugin {
    virtual void build(App& app) = 0;
};

struct AppRunner {
    virtual int run(App& app) = 0;
};
struct AppCreateInfo {
    bool mark_frame            = true;
    bool enable_tracy          = false;
    uint32_t control_pool_size = 2;
    uint32_t default_pool_size = 4;
};
struct Schedules : public entt::dense_map<ScheduleLabel, Schedule*> {
    EPIX_API Schedule* get(const ScheduleLabel& label);
    EPIX_API const Schedule* get(const ScheduleLabel& label) const;
};
struct App {
    using executor_t = BS::thread_pool<BS::tp::priority>;

    struct TracySettings {
       private:
        entt::dense_map<ScheduleLabel, bool> schedules_enable_tracy;

       public:
        bool mark_frame   = true;
        bool enable_tracy = false;
        EPIX_API TracySettings& schedule_enable_tracy(
            const ScheduleLabel& label, bool enable = true
        );
        EPIX_API bool schedule_enabled_tracy(const ScheduleLabel& label);
    };

   private:
    // worlds
    entt::dense_map<WorldLabel, std::unique_ptr<World>> m_worlds;
    // schedules
    entt::dense_map<GroupLabel, ScheduleGroup> m_schedule_groups;
    // executors
    std::shared_ptr<Executors> m_executors;
    // control pools
    std::shared_ptr<executor_t> m_control_pool;
    // runner, shared_ptr to automatic destruction
    std::shared_ptr<AppRunner> m_runner;

    TracySettings m_tracy_settings;

    // logger
    std::shared_ptr<spdlog::logger> m_logger;

    // plugins
    std::vector<std::pair<std::type_index, std::shared_ptr<Plugin>>> m_plugins;
    entt::dense_set<std::type_index> m_built_plugins;

    // queued systems and sets for deferred add
    std::vector<std::pair<ScheduleInfo, SystemConfig>> m_queued_systems;
    std::vector<std::pair<ScheduleLabel, SystemSetConfig>> m_queued_sets;
    std::vector<SystemSetConfig> m_queued_all_sets;

    mutable std::unique_ptr<std::shared_mutex> m_mutex;

    EPIX_API std::optional<GroupLabel> find_belonged_group(
        const ScheduleLabel& label
    ) const noexcept;
    template <typename Ret, typename T>
    App& plugin_scope_internal(const std::function<Ret(T&)>& func) {
        auto plugin = get_plugin<std::decay_t<T>>();
        if (plugin) {
            func(*plugin);
        }
        return *this;
    }

   public:
    EPIX_API App(const AppCreateInfo& create_info);
    App(const App&)            = delete;
    App(App&&)                 = default;
    App& operator=(const App&) = delete;
    App& operator=(App&&)      = default;

    EPIX_API static App create(const AppCreateInfo& create_info = {});

    EPIX_API App& set_logger(std::shared_ptr<spdlog::logger> logger);
    EPIX_API std::shared_ptr<spdlog::logger> logger();

    // Access

    EPIX_API TracySettings& tracy_settings();
    EPIX_API const TracySettings& tracy_settings() const;

    EPIX_API World& world(const WorldLabel& label);
    EPIX_API World* get_world(const WorldLabel& label);

    EPIX_API Executors& executors();
    EPIX_API std::shared_ptr<Executors> get_executors();

    EPIX_API executor_t& control_pool();
    EPIX_API std::shared_ptr<executor_t> get_control_pool();

    EPIX_API Schedule& schedule(const ScheduleLabel& label);
    EPIX_API Schedule* get_schedule(const ScheduleLabel& label);

    EPIX_API ScheduleGroup& schedule_group(const GroupLabel& label);
    EPIX_API ScheduleGroup* get_schedule_group(const GroupLabel& label);

    // Modify. These modifications modify App owned data. Need to lock to avoid
    // other threads access the data at the same time, which will result in
    // invalid references.

    EPIX_API App& add_world(const WorldLabel& label);
    EPIX_API App& add_schedule_group(
        const GroupLabel& label, ScheduleGroup&& group = {}
    );
    EPIX_API App& add_schedule(
        const GroupLabel& label, ScheduleConfig&& config
    );
    EPIX_API App& add_schedule(const GroupLabel& label, ScheduleConfig& config);
    EPIX_API App& schedule_sequence(
        const ScheduleLabel& scheduleId, const ScheduleLabel& otherId
    );
    EPIX_API App& schedule_set_src(
        const ScheduleLabel& scheduleId, const WorldLabel& worldId
    );
    EPIX_API App& schedule_set_dst(
        const ScheduleLabel& scheduleId, const WorldLabel& worldId
    );
    template <typename... Labels>
        requires(sizeof...(Labels) > 1)
    App& schedule_sequence(const ScheduleLabel& label, Labels&&... labels) {
        std::vector<ScheduleLabel> labels_vec;
        labels_vec.reserve(sizeof...(labels) + 1);
        labels_vec.emplace_back(label);
        (labels_vec.emplace_back(std::forward<Labels>(labels)), ...);
        for (size_t i = 0; i < labels_vec.size() - 1; ++i) {
            schedule_sequence(labels_vec[i], labels_vec[i + 1]);
        }
        return *this;
    };

    // Member modify. These modifications does not modify App owned data, but
    // needs valid references of App owned data.

    EPIX_API App& add_systems(const ScheduleInfo& label, SystemConfig&& config);
    EPIX_API App& add_systems(const ScheduleInfo& label, SystemConfig& config);

    template <typename T, typename... Args>
    App& add_plugin(Args&&... args) {
        using type = std::decay_t<T>;
        std::unique_lock lock(*m_mutex);
        if (std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [](const auto& plugin) {
                    return plugin.first == std::type_index(typeid(type));
                }
            ) != m_plugins.end()) {
            // plugin already exists, do nothing
            return *this;
        }
        m_plugins.emplace_back(
            std::type_index(typeid(type)), std::make_shared<type>(args...)
        );
        return *this;
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        using type = std::decay_t<T>;
        std::unique_lock lock(*m_mutex);
        if (std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [](const auto& plugin) {
                    return plugin.first == std::type_index(typeid(type));
                }
            ) != m_plugins.end()) {
            // plugin already exists, do nothing
            return *this;
        }
        m_plugins.emplace_back(
            std::type_index(typeid(type)), std::make_shared<type>(plugin)
        );
        return *this;
    }
    template <typename... Plugins>
    App& add_plugins(Plugins&&... plugins) {
        (add_plugin<std::decay_t<Plugins>>(std::forward<Plugins>(plugins)),
         ...);
        return *this;
    }
    template <typename T>
    std::shared_ptr<T> get_plugin() {
        std::shared_lock lock(*m_mutex);
        auto it = std::find_if(
            m_plugins.begin(), m_plugins.end(),
            [](const auto& plugin) {
                return plugin.first == std::type_index(typeid(T));
            }
        );
        if (it != m_plugins.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }
    template <typename T>
    App& plugin_scope(T&& func) {
        plugin_scope_internal(std::function(std::forward<T>(func)));
        return *this;
    }

    EPIX_API App& configure_sets(
        const ScheduleLabel& label, const SystemSetConfig& config
    );
    EPIX_API App& configure_sets(const SystemSetConfig& config);

    EPIX_API App& remove_system(
        const ScheduleLabel& id, const SystemLabel& label
    );
    EPIX_API App& remove_set(
        const ScheduleLabel& id, const SystemSetLabel& label
    );
    EPIX_API App& remove_set(const SystemSetLabel& label);

    // Main world Initialization

    EPIX_API App& add_resource(const UntypedRes& resource);
    template <typename T, typename... Args>
    App& emplace_resource(Args&&... args) {
        if (auto* w = get_world(MainWorld)) {
            w->emplace_resource<T>(std::forward<Args>(args)...);
        }
        return *this;
    };
    template <typename T>
    App& insert_resource(T&& resource) {
        if (auto* w = get_world(MainWorld)) {
            w->insert_resource(std::forward<T>(resource));
        }
        return *this;
    };
    template <typename T>
    App& init_resource() {
        if (auto* w = get_world(MainWorld)) {
            w->init_resource<T>();
        }
        return *this;
    };
    template <typename T>
    App& add_events() {
        init_resource<Events<T>>();
        add_systems(
            Last, into([](ResMut<Events<T>> event) { event->update(); }
                  ).set_name(std::format("update Event<{}>", typeid(T).name()))
        );
        // UntypedRes res = w->untyped_resource(typeid(Events<T>));
        // for (auto&& [label, world] : m_worlds) {
        //     if (label != WorldLabel(MainWorld)) {
        //         world->add_resource(res);
        //     }
        // }
        return *this;
    }
    template <typename T>
    App& init_state() {
        insert_resource(T{});
    }
    template <typename T>
    App& insert_state(T&& state) {
        emplace_resource<State<T>>(std::forward<T>(state));
        emplace_resource<NextState<T>>(std::forward<T>(state));
        add_systems(
            StateTransition,
            into([](ResMut<State<T>> state, Res<NextState<T>> next) {
                *state = *next;
            })
                .set_label(std::format("update State<{}>", typeid(T).name()))
                .in_set(StateTransitionSet::Transit)
        );
        return *this;
    };

    // Run
    EPIX_API void build();
    EPIX_API void set_runner(std::shared_ptr<AppRunner> runner);
    EPIX_API int run();
    EPIX_API int run_group(const GroupLabel& label);
    EPIX_API void run_schedule(const ScheduleLabel& label);
    template <typename Ret>
    std::expected<Ret, bool> run_system(
        BasicSystem<Ret>& system,
        const WorldLabel& src = MainWorld,
        const WorldLabel& dst = MainWorld
    ) {
        auto wsrc = get_world(src);
        auto wdst = get_world(dst);
        if (!wsrc || !wdst) {
            return std::unexpected(false);
        }
        return system.run(*wsrc, *wdst);
    };
    template <typename Func>
    std::expected<
        typename decltype(std::function(std::declval<Func>()))::return_type,
        bool>
    run_system(
        Func&& func,
        const WorldLabel& src = MainWorld,
        const WorldLabel& dst = MainWorld
    ) {
        using func_type   = decltype(std::function(func));
        using return_type = typename func_type::return_type;
        BasicSystem<return_type> system(func);
        auto wsrc = get_world(src);
        auto wdst = get_world(dst);
        if (!wsrc || !wdst) {
            return std::unexpected(false);
        }
        return system.run(*wsrc, *wdst);
    };
};

struct AppExit {
    int code = 0;
};
struct LoopPlugin : public Plugin {
    bool m_loop_enabled = true;
    EPIX_API void build(App& app) override;
    EPIX_API LoopPlugin& set_enabled(bool enabled);
};
}  // namespace epix::app