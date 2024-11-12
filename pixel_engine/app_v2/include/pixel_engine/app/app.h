#pragma once

#include "runner.h"

namespace pixel_engine::prelude {
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
}  // namespace pixel_engine::prelude
namespace pixel_engine::app {
using namespace pixel_engine::prelude;
struct AppExit {};
struct App;
struct MainSubApp {};
struct RenderSubApp {};
struct Plugin {
    virtual void build(App&) = 0;
};
struct App {
    static App create();
    struct SystemInfo {
        SystemNode* node;
        App* app;

        template <typename... Ptrs>
        SystemInfo& before(Ptrs... ptrs) {
            (node->before(ptrs), ...);
            return *this;
        }
        template <typename... Ptrs>
        SystemInfo& after(Ptrs... ptrs) {
            (node->after(ptrs), ...);
            return *this;
        }
        template <typename... Args>
        SystemInfo& run_if(std::function<bool(Args...)> func) {
            node->m_conditions.emplace_back(
                std::make_unique<Condition<Args...>>(func)
            );
            return *this;
        }
        template <typename... Args>
        SystemInfo& use_worker(const std::string& pool_name) {
            node->m_worker = pool_name;
            return *this;
        }
        template <typename... Sets>
        SystemInfo& in_set(Sets... sets) {
            (node->m_in_sets.push_back(SystemSet(sets)), ...);
            return *this;
        }
        template <typename T>
        SystemInfo& on_enter(T state) {
            if (app->m_runner->stage_state_transition(node->m_stage.m_stage)) {
                node->m_conditions.emplace_back(
                    std::make_unique<
                        Condition<Res<State<T>>, Res<NextState<T>>>>(
                        [state](Res<State<T>> s, Res<NextState<T>> ns) {
                            return (s->is_state(state) && s->is_just_created()
                                   ) ||
                                   (ns->is_state(state) && !s->is_state(state));
                        }
                    )
                );
            } else {
                spdlog::warn(
                    "adding system {:#018x} to stage {} is not allowed"
                    "on_enter can only be used in state transition stages",
                    (size_t)node->m_sys_addr, node->m_stage.m_stage.name()
                );
            }
            return *this;
        }
        template <typename T>
        SystemInfo& on_exit(T state) {
            if (app->m_runner->stage_state_transition(node->m_stage.m_stage)) {
                node->m_conditions.emplace_back(
                    std::make_unique<
                        Condition<Res<State<T>>, Res<NextState<T>>>>(
                        [state](Res<State<T>> s, Res<NextState<T>> ns) {
                            return !ns->is_state(state) && s->is_state(state);
                        }
                    )
                );
            } else {
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
            if (app->m_runner->stage_state_transition(node->m_stage.m_stage)) {
                node->m_conditions.emplace_back(
                    std::make_unique<
                        Condition<Res<State<T>>, Res<NextState<T>>>>(
                        [](Res<State<T>> s, Res<NextState<T>> ns) {
                            return !s->is_state(ns);
                        }
                    )
                );
            } else {
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
            node->m_conditions.emplace_back(
                std::make_unique<Condition<Res<State<T>>>>(
                    [state](Res<State<T>> s) { return s.is_state(state); }
                )
            );
            return *this;
        }

        App* operator->();
    };

    template <typename StageT, typename... Args>
    SystemInfo add_system(StageT stage, void (*func)(Args...)) {
        SystemNode* ptr = m_runner->add_system(stage, func);
        return {ptr, this};
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        m_plugins.emplace(
            std::type_index(typeid(std::remove_reference_t<T>)),
            std::make_shared<T>(std::forward<T>(plugin))
        );
        return *this;
    }
    template <typename T>
    std::shared_ptr<T> get_plugin() {
        return std::static_pointer_cast<T>(m_plugins[std::type_index(typeid(T))]);
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
    template <typename... Sets>
    App& configure_sets(Sets... sets) {
        m_runner->configure_sets(sets...);
        return *this;
    }

    void run();
    void set_log_level(spdlog::level::level_enum level);
    App& enable_loop();
    App& disable_loop();
    App* operator->();

   protected:
    App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    App(App&&)                 = default;
    App& operator=(App&&)      = default;

    template <typename T>
    void add_sub_app() {
        m_sub_apps->emplace(std::type_index(typeid(T)), std::make_unique<SubApp>());
    }

    void build_plugins();
    void build();
    void end_commands();
    void tick_events();
    void update_states();

    std::unique_ptr<
        spp::sparse_hash_map<std::type_index, std::unique_ptr<SubApp>>>
        m_sub_apps;
    std::unique_ptr<Runner> m_runner;
    spp::sparse_hash_map<std::type_index, std::shared_ptr<Plugin>> m_plugins;
    std::unique_ptr<BasicSystem<bool>> m_check_exit_func;
    bool m_loop_enabled = false;

    std::shared_ptr<spdlog::logger> m_logger;
};
}  // namespace pixel_engine::app