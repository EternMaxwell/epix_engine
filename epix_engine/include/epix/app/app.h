#pragma once

#include "events.h"
#include "profiler.h"
#include "schedule.h"
#include "schedule_info.h"
#include "state.h"

namespace epix::app {
struct App;

struct Plugin {
    virtual void build(App& app) = 0;
};

struct AppRunner {
    virtual int run(App& app) = 0;
    virtual ~AppRunner()      = default;
};
struct AppCreateInfo {
    bool mark_frame            = false;
    bool enable_tracy          = false;
    uint32_t control_pool_size = 2;
    uint32_t default_pool_size = 4;
};
struct Schedules : public entt::dense_map<ScheduleLabel, Schedule*> {
    EPIX_API Schedule* get(const ScheduleLabel& label);
    EPIX_API const Schedule* get(const ScheduleLabel& label) const;
};
struct AppLabel : public WorldLabel {
    using WorldLabel::WorldLabel;
};
/**
 * @brief App data structure.
 *
 * Used in App to store various data related to the application.
 * This is not movable or copyable, should be used as shared pointer or
 * reference.
 */
struct AppData {
    const AppLabel label;
    async::RwLock<World> world;
    async::RwLock<entt::dense_map<ScheduleLabel, std::unique_ptr<Schedule>>>
        schedules;
    async::RwLock<std::vector<ScheduleLabel>> extract_schedule_order;
    async::RwLock<std::vector<ScheduleLabel>> main_schedule_order;
    async::RwLock<std::vector<ScheduleLabel>> exit_schedule_order;
    async::RwLock<
        std::vector<std::pair<meta::type_index, std::shared_ptr<Plugin>>>>
        plugins;
    async::RwLock<entt::dense_set<meta::type_index>> built_plugins;

    async::RwLock<std::vector<std::pair<ScheduleLabel, SystemSetConfig>>>
        queued_sets;
    async::RwLock<std::vector<SystemSetConfig>> queued_all_sets;
    async::RwLock<std::vector<std::pair<ScheduleInfo, SystemSetConfig>>>
        queued_systems;

    mutable async::RwLock<std::shared_ptr<RunState>> run_state;

    EPIX_API AppData(const AppLabel& world_label);
};
inline struct MainT {
} Main;

struct EventSystem : public Plugin {
   private:
    std::vector<void (*)(World&)> updates;

   public:
    template <typename T>
    void register_event() {
        updates.emplace_back([](World& world) {
            if (auto events = world.get_resource<Events<T>>()) {
                events->update();
            }
        });
    }
    EPIX_API void build(App& app) override;
};
struct App {
    using executor_t = BS::thread_pool<BS::tp::priority>;

    struct AppConfig {
        bool mark_frame   = false;
        bool enable_tracy = false;
    } config;

   private:
    // data
    std::unique_ptr<AppData> m_data;
    async::RwLock<entt::dense_map<AppLabel, App>> m_sub_apps;
    // executors
    std::shared_ptr<Executors> m_executors;
    // runner, shared_ptr to automatic destruction
    async::RwLock<std::unique_ptr<AppRunner>> m_runner;

    template <typename Ret, typename T>
    App& plugin_scope_internal(const std::function<Ret(T&)>& func) {
        auto pplugins = m_data->plugins.read();
        auto& plugins = *pplugins;
        auto it       = std::find_if(
            plugins.begin(), plugins.end(),
            [](const auto& plugin) {
                return plugin.first == meta::type_index(meta::type_id<T>());
            }
        );
        if (it != plugins.end()) {
            func(*std::static_pointer_cast<T>(it->second));
        }
        return *this;
    }
    EPIX_API App(const AppLabel& label);

   public:
    App(const App&)            = delete;
    App(App&&)                 = default;
    App& operator=(const App&) = delete;
    App& operator=(App&&)      = default;
    EPIX_API ~App();

    EPIX_API static App create(const AppCreateInfo& create_info = {});

    EPIX_API async::RwLock<World>::WriteGuard world();
    EPIX_API async::RwLock<World>::ReadGuard world() const;

    EPIX_API Schedule& schedule(const ScheduleLabel& label);
    EPIX_API Schedule* get_schedule(const ScheduleLabel& label);

    EPIX_API App& sub_app(const AppLabel& label);
    EPIX_API App* get_sub_app(const AppLabel& label);

    EPIX_API std::shared_ptr<RunState> run_state() const;
    EPIX_API void reset_run_state() const;

    // Modify. These modifications modify App owned data. Need to lock to avoid
    // other threads access the data at the same time, which will result in
    // invalid references.

    EPIX_API App& add_sub_app(const AppLabel& label);
    EPIX_API App& add_schedule(Schedule&& schedule);
    EPIX_API App& add_schedule(const ScheduleLabel& label);
    EPIX_API App& main_schedule_order(
        const ScheduleLabel& left,
        std::optional<ScheduleLabel> right = std::nullopt
    );
    EPIX_API App& exit_schedule_order(
        const ScheduleLabel& left,
        std::optional<ScheduleLabel> right = std::nullopt
    );
    EPIX_API App& extract_schedule_order(
        const ScheduleLabel& left,
        std::optional<ScheduleLabel> right = std::nullopt
    );

    // Member modify. These modifications does not modify App owned data, but
    // needs valid references of App owned data.

    template <typename T, typename... Args>
    App& add_plugin(Args&&... args) {
        using type    = std::decay_t<T>;
        auto pplugins = m_data->plugins.write();
        auto& plugins = *pplugins;
        if (std::find_if(
                plugins.begin(), plugins.end(),
                [](const auto& plugin) {
                    return plugin.first ==
                           meta::type_index(meta::type_id<type>());
                }
            ) != plugins.end()) {
            // plugin already exists, do nothing
            return *this;
        }
        plugins.emplace_back(
            meta::type_index(meta::type_id<type>()),
            std::make_shared<type>(std::forward<Args>(args)...)
        );
        return *this;
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        using type    = std::decay_t<T>;
        auto pplugins = m_data->plugins.write();
        auto& plugins = *pplugins;
        if (std::find_if(
                plugins.begin(), plugins.end(),
                [](const auto& plugin) {
                    return plugin.first ==
                           meta::type_index(meta::type_id<type>());
                }
            ) != plugins.end()) {
            // plugin already exists, do nothing
            return *this;
        }
        plugins.emplace_back(
            meta::type_index(meta::type_id<type>()),
            std::make_shared<type>(std::forward<T>(plugin))
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
        auto pplugins = m_data->plugins.read();
        auto& plugins = *pplugins;
        auto it       = std::find_if(
            plugins.begin(), plugins.end(),
            [](const auto& plugin) {
                return plugin.first == meta::type_index(meta::type_id<T>());
            }
        );
        if (it != plugins.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }
    template <typename T>
    App& plugin_scope(T&& func) {
        plugin_scope_internal(std::function(std::forward<T>(func)));
        return *this;
    }

    EPIX_API App& add_systems(
        const ScheduleInfo& label, SystemSetConfig&& config
    );
    EPIX_API App& add_systems(
        const ScheduleInfo& label, SystemSetConfig& config
    );
    EPIX_API App& configure_sets(
        const ScheduleLabel& label, const SystemSetConfig& config
    );
    EPIX_API App& configure_sets(const SystemSetConfig& config);
    EPIX_API App& remove_system(
        const ScheduleLabel& id, const SystemSetLabel& label
    );
    EPIX_API App& remove_set(
        const ScheduleLabel& id, const SystemSetLabel& label
    );
    EPIX_API App& remove_set(const SystemSetLabel& label);

    template <typename T, typename... Args>
    App& emplace_resource(Args&&... args) {
        auto w = world();
        w->emplace_resource<T>(std::forward<Args>(args)...);
        return *this;
    };
    template <typename T>
    App& insert_resource(T&& resource) {
        auto w = world();
        w->insert_resource(std::forward<T>(resource));
        return *this;
    };
    template <typename T>
    App& init_resource() {
        auto w = world();
        w->init_resource<T>();
        return *this;
    };
    template <typename T>
    App& add_resource(std::shared_ptr<T>&& res) {
        auto w = world();
        w->add_resource(std::move(res));
        return *this;
    };
    template <typename T>
    T& resource() {
        auto w = world();
        return w->resource<T>();
    };
    template <typename T>
    const T& resource() const {
        auto w = world();
        return w->resource<T>();
    };
    template <typename T>
    T* get_resource() {
        auto w = world();
        return w->get_resource<T>();
    };
    template <typename T>
    const T* get_resource() const {
        auto w = world();
        return w->get_resource<T>();
    };
    template <typename... Args>
    auto spawn(Args&&... args) {
        auto w = world();
        return w->spawn(std::forward<Args>(args)...);
    };
    template <typename T>
    App& add_events() {
        init_resource<Events<T>>();
        add_plugin(EventSystem{});
        plugin_scope([](EventSystem& event_system) {
            event_system.register_event<T>();
        });
        return *this;
    }
    template <typename T>
    App& init_state() {
        insert_state(T{});
    }
    template <typename T>
    App& insert_state(T&& state) {
        using type = std::decay_t<T>;
        emplace_resource<State<type>>(std::forward<T>(state));
        emplace_resource<NextState<type>>(std::forward<T>(state));
        add_systems(
            StateTransition,
            into([](ResMut<State<type>> state, Res<NextState<type>> next) {
                *state = *next;
            })
                .set_label(std::format("update State<{}>", typeid(T).name()))
                .in_set(StateTransitionSet::Transit)
        );
        return *this;
    };

    // Run
    EPIX_API void build();
    EPIX_API std::future<void> extract(App& target);
    EPIX_API std::future<void> update();
    EPIX_API std::future<void> exit();
    EPIX_API void set_runner(std::unique_ptr<AppRunner>&& runner);
    EPIX_API int run();
    template <typename Ret>
    std::expected<Ret, RunSystemError> run_system(BasicSystem<Ret>* system) {
        if (!system) {
            return std::unexpected(RunSystemError{SystemExceptionError{
                std::make_exception_ptr(std::runtime_error("System is null"))
            }});
        }
        auto write = world();
        if (!system->initialized()) {
            system->initialize(*write);
        }
        return system->run(*write);
    };
    template <typename Func>
    std::expected<
        typename decltype(std::function(std::declval<Func>()))::result_type,
        RunSystemError>
    run_system(Func&& func) {
        using func_type   = decltype(std::function(func));
        using return_type = typename func_type::result_type;
        auto system       = IntoSystem::into_system(std::forward<Func>(func));
        auto write        = world();
        system->initialize(*write);
        return system->run(*write);
    };
    template <typename Func>
    auto submit_system(const app::ExecutorLabel& executor, Func&& func)
        -> std::future<decltype(run_system(std::declval<Func>()))> {
        auto write  = world();
        auto system = IntoSystem::into_system(std::forward<Func>(func));
        system->initialize(*write);
        auto pool = m_executors->get_pool(executor);
        if (pool) {
            return pool->submit_task([system = std::move(system),
                                      write  = std::move(write)]() mutable {
                return system->run(*write);
            });
        }
        std::promise<decltype(run_system(std::declval<Func>()))> promise;
        promise.set_value(system->run(*write));
        return promise.get_future();
    }
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