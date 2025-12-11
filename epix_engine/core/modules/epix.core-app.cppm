/**
 * @file epix.core-app.cppm
 * @brief App partition for application framework
 */

export module epix.core:app;

import :fwd;
import :world;
import :schedule;
import :system;
import :event;
import :label;

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

export namespace epix::core::app {
    // App labels for scheduling
    struct First : Label { First() : Label("First") {} };
    struct PreStartup : Label { PreStartup() : Label("PreStartup") {} };
    struct Startup : Label { Startup() : Label("Startup") {} };
    struct PostStartup : Label { PostStartup() : Label("PostStartup") {} };
    struct PreUpdate : Label { PreUpdate() : Label("PreUpdate") {} };
    struct Update : Label { Update() : Label("Update") {} };
    struct PostUpdate : Label { PostUpdate() : Label("PostUpdate") {} };
    struct PreExit : Label { PreExit() : Label("PreExit") {} };
    struct Exit : Label { Exit() : Label("Exit") {} };
    struct PostExit : Label { PostExit() : Label("PostExit") {} };
    struct Last : Label { Last() : Label("Last") {} };
    
    // State management
    template <typename S>
    struct State {
        S current_state;
        S next_state;
        
        const S& get() const { return current_state; }
        void set(S state) { next_state = std::move(state); }
    };
    
    template <typename S>
    struct NextState {
        State<S>* state;
        
        void set(S s) { state->set(std::move(s)); }
    };
    
    template <typename S>
    struct OnEnter : Label {
        S state;
        OnEnter(S s) : Label("OnEnter"), state(std::move(s)) {}
    };
    
    template <typename S>
    struct OnExit : Label {
        S state;
        OnExit(S s) : Label("OnExit"), state(std::move(s)) {}
    };
    
    template <typename S>
    struct OnChange : Label {
        S from;
        S to;
        OnChange(S f, S t) : Label("OnChange"), from(std::move(f)), to(std::move(t)) {}
    };
    
    struct StateTransition : Label {
        StateTransition() : Label("StateTransition") {}
    };
    
    // App exit event
    struct AppExit {
        int code = 0;
    };
    
    // Plugin interface
    struct Plugin {
        virtual ~Plugin() = default;
        virtual void build(App& app) = 0;
    };
    
    // Extract trait for rendering
    template <typename T>
    struct Extract {
        virtual ~Extract() = default;
        virtual T extract(const World& world) = 0;
    };
    
    // App runner
    struct AppRunner {
        virtual ~AppRunner() = default;
        virtual void run(App& app) = 0;
    };
    
    // Loop plugin
    struct LoopPlugin : Plugin {
        void build(App& app) override;
    };
    
    // Main App structure
    struct App {
       private:
        World _world;
        std::unordered_map<Label, schedule::Schedule> _schedules;
        std::vector<std::unique_ptr<Plugin>> _plugins;
        std::unique_ptr<AppRunner> _runner;
        
       public:
        static App create();
        
        App& add_plugins(Plugin* plugin);
        
        template <typename P>
            requires std::derived_from<P, Plugin>
        App& add_plugins(P plugin) {
            _plugins.push_back(std::make_unique<P>(std::move(plugin)));
            _plugins.back()->build(*this);
            return *this;
        }
        
        template <typename F, typename... Ts>
        App& add_systems(Label label, F&& func, Ts&&... funcs);
        
        template <typename P, typename F>
        App& plugin_scope(F&& func);
        
        App& set_runner(std::unique_ptr<AppRunner> runner);
        
        int run();
        
        World& world() { return _world; }
        const World& world() const { return _world; }
        
        schedule::Schedule& get_schedule(Label label);
        
        template <typename T, typename... Args>
        App& init_resource(Args&&... args);
        
        template <typename E>
        App& add_event();
    };
    
    // App label type
    using AppLabel = Label;
}  // namespace epix::core::app
