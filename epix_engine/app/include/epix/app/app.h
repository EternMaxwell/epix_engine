#pragma once

#include "common.h"
#include "runner.h"

namespace epix::prelude {
enum MainStartupStage {
    PreStartup,
    Startup,
    PostStartup,
};
enum MainTransitStage {
    Transit,
};
enum MainLoopStage {
    First,
    PreUpdate,
    Update,
    PostUpdate,
    Last,
};
enum MainExitStage {
    PreExit,
    Exit,
    PostExit,
};
enum RenderStartupStage {
    PreRenderStartup,
    RenderStartup,
    PostRenderStartup,
};
enum RenderLoopStage {
    Prepare,
    PreRender,
    Render,
    PostRender,
};
enum RenderExitStage {
    PreRenderExit,
    RenderExit,
    PostRenderExit,
};
}  // namespace epix::prelude
namespace epix::app {
using namespace epix::prelude;
struct AppExit {};
struct App;
struct MainSubApp {};
struct RenderSubApp {};
struct Plugin {
    virtual void build(App&) = 0;
};
struct App {
    EPIX_API static App create();
    struct SystemInfo {
        std::vector<SystemNode*> nodes;
        App* app;

        template <typename... Ptrs>
        SystemInfo& before(Ptrs... ptrs) {
            for (auto node : nodes) (node->before(ptrs), ...);
            return *this;
        }
        template <typename... Ptrs>
        SystemInfo& after(Ptrs... ptrs) {
            for (auto node : nodes) (node->after(ptrs), ...);
            return *this;
        }
        template <typename... Args>
        SystemInfo& run_if(std::function<bool(Args...)> func) {
            for (auto node : nodes)
                node->m_conditions.emplace_back(
                    std::make_unique<Condition<Args...>>(func)
                );
            return *this;
        }
        template <typename... Args>
        SystemInfo& use_worker(const std::string& pool_name) {
            for (auto node : nodes) node->m_worker = pool_name;
            return *this;
        }
        template <typename... Sets>
        SystemInfo& in_set(Sets... sets) {
            for (auto node : nodes)
                (node->m_in_sets.push_back(SystemSet(sets)), ...);
            return *this;
        }
        template <typename T>
        SystemInfo& on_enter(T state) {
            if (app->m_runner->stage_state_transition(nodes[0]->m_stage.m_stage
                )) {
                for (auto node : nodes)
                    node->m_conditions.emplace_back(
                        std::make_unique<
                            Condition<Res<State<T>>, Res<NextState<T>>>>(
                            [state](Res<State<T>> s, Res<NextState<T>> ns) {
                                return (s->is_state(state) &&
                                        s->is_just_created()) ||
                                       (ns->is_state(state) &&
                                        !s->is_state(state));
                            }
                        )
                    );
            } else {
                for (auto node : nodes)
                    spdlog::warn(
                        "adding system {:#018x} to stage {} is not allowed"
                        "on_enter can only be used in state transition stages",
                        (size_t)node->m_sys_addr.func,
                        node->m_stage.m_stage.name()
                    );
            }
            return *this;
        }
        template <typename T>
        SystemInfo& on_exit(T state) {
            if (app->m_runner->stage_state_transition(nodes[0]->m_stage.m_stage
                )) {
                for (auto node : nodes)
                    node->m_conditions.emplace_back(
                        std::make_unique<
                            Condition<Res<State<T>>, Res<NextState<T>>>>(
                            [state](Res<State<T>> s, Res<NextState<T>> ns) {
                                return !ns->is_state(state) &&
                                       s->is_state(state);
                            }
                        )
                    );
            } else {
                for (auto node : nodes)
                    spdlog::warn(
                        "adding system {:#018x} to stage {} is not allowed"
                        "on_exit can only be used in state transition stages",
                        (size_t)node->m_sys_addr, node->m_stage.m_stage->name()
                    );
            }
            return *this;
        }
        template <typename T>
        SystemInfo& on_change() {
            if (app->m_runner->stage_state_transition(nodes[0]->m_stage.m_stage
                )) {
                for (auto node : nodes)
                    node->m_conditions.emplace_back(
                        std::make_unique<
                            Condition<Res<State<T>>, Res<NextState<T>>>>(
                            [](Res<State<T>> s, Res<NextState<T>> ns) {
                                return !s->is_state(ns);
                            }
                        )
                    );
            } else {
                for (auto node : nodes)
                    spdlog::warn(
                        "adding system {:#018x} to stage {} is not allowed"
                        "on_change can only be used in state transition stages",
                        (size_t)node->m_sys_addr, node->m_stage.m_stage->name()
                    );
            }
            return *this;
        }
        template <typename T>
        SystemInfo& in_state(T state) {
            for (auto node : nodes)
                node->m_conditions.emplace_back(
                    std::make_unique<Condition<Res<State<T>>>>(
                        [state](Res<State<T>> s) { return s->is_state(state); }
                    )
                );
            return *this;
        }
        EPIX_API SystemInfo& chain();

        EPIX_API App* operator->();
    };

    template <typename StageT, typename... Args>
    SystemInfo add_system(StageT stage, void (*func)(Args...)) {
        SystemNode* ptr = m_runner->add_system(stage, func);
        return {{ptr}, this};
    }
    template <typename StageT, typename... Funcs>
    SystemInfo add_system(StageT stage, Funcs... funcs) {
        std::vector<SystemNode*> nodes = {m_runner->add_system(stage, funcs)...
        };
        return {std::move(nodes), this};
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        if (m_plugin_types.find(
                std::type_index(typeid(std::remove_reference_t<T>))
            ) != m_plugin_types.end()) {
            spdlog::warn(
                "Plugin {} already exists, skipping",
                typeid(std::remove_reference_t<T>).name()
            );
            return *this;
        }
        m_plugins.emplace_back(
            std::type_index(typeid(std::remove_reference_t<T>)),
            std::make_shared<std::remove_reference_t<T>>(std::forward<T>(plugin)
            )
        );
        return *this;
    }
    template <typename T>
    std::shared_ptr<T> get_plugin() {
        return std::static_pointer_cast<T>(
            std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [](const auto& pair) {
                    return pair.first == std::type_index(typeid(T));
                }
            )->second
        );
    }
    template <typename T, typename Target = MainSubApp, typename... Args>
    App& emplace_resource(Args&&... args) {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add resource to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->emplace_resource<T>(std::forward<Args>(args)...);
        return *this;
    }
    template <typename Target = MainSubApp, typename T>
    App& insert_resource(T&& res) {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add resource to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->insert_resource(std::forward<T>(res)...);
        return *this;
    }
    template <typename T, typename Target = MainSubApp>
    App& init_resource() {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add resource to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->init_resource<T>();
        return *this;
    }
    template <typename T, typename Target = MainSubApp>
    App& insert_state(T&& state) {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add state to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->insert_state(std::forward<T>(state));
        return *this;
    }
    template <typename T, typename Target = MainSubApp>
    App& init_state() {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add state to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->init_state<T>();
        return *this;
    }
    template <typename EventT, typename Target = MainSubApp>
    App& add_event() {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add state to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->add_event<EventT>();
        return *this;
    }
    template <typename... Sets>
    App& configure_sets(Sets... sets) {
        m_runner->configure_sets(sets...);
        return *this;
    }

    EPIX_API void run();
    EPIX_API void set_log_level(spdlog::level::level_enum level);
    EPIX_API App& enable_loop();
    EPIX_API App& disable_loop();
    EPIX_API App* operator->();

   protected:
    EPIX_API App();
    EPIX_API App(const App&)            = delete;
    EPIX_API App& operator=(const App&) = delete;
    EPIX_API App(App&&)                 = default;
    EPIX_API App& operator=(App&&)      = default;

    template <typename T>
    void add_sub_app() {
        m_sub_apps->emplace(
            std::type_index(typeid(T)), std::make_unique<SubApp>()
        );
    }

    EPIX_API void build_plugins();
    EPIX_API void build();
    EPIX_API void end_commands();
    EPIX_API void tick_events();
    EPIX_API void update_states();

    std::unique_ptr<
        spp::sparse_hash_map<std::type_index, std::unique_ptr<SubApp>>>
        m_sub_apps;
    std::unique_ptr<Runner> m_runner;
    std::vector<std::pair<std::type_index, std::shared_ptr<Plugin>>> m_plugins;
    spp::sparse_hash_set<std::type_index> m_plugin_types;
    std::unique_ptr<BasicSystem<bool>> m_check_exit_func;
    bool m_loop_enabled = false;

    std::shared_ptr<spdlog::logger> m_logger;
};
}  // namespace epix::app