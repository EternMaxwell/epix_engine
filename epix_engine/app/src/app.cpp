#include "epix/app.h"

using namespace epix::app;

EPIX_API App App::create() {
    App app;
    app.runner().assign_startup_stage<MainSubApp, MainSubApp>(
        PreStartup, Startup, PostStartup
    );
    app.runner().assign_state_transition_stage<MainSubApp, MainSubApp>(Transit);
    app.runner().assign_loop_stage<MainSubApp, MainSubApp>(
        First, PreUpdate, Update, PostUpdate, Last
    );
    app.runner()
        .assign_loop_stage<MainSubApp, MainSubApp>(
            Prepare, PreRender, Render, PostRender
        )
        .add_prev_stage<MainLoopStage>();
    // this is temporary, currently the render stages are still in main
    // subapp
    app.runner().assign_exit_stage<MainSubApp, MainSubApp>(
        PreExit, Exit, PostExit
    );
    return std::move(app);
}
EPIX_API App App::create2() {
    App app;
    app.add_sub_app<RenderSubApp>();
    app.runner().assign_startup_stage<MainSubApp, MainSubApp>(
        PreStartup, Startup, PostStartup
    );
    app.runner().assign_state_transition_stage<MainSubApp, MainSubApp>(Transit);
    app.runner().assign_loop_stage<MainSubApp, RenderSubApp>(
        PreExtract, Extraction, PostExtract
    );
    app.runner()
        .assign_loop_stage<MainSubApp, MainSubApp>(
            First, PreUpdate, Update, PostUpdate, Last
        )
        .add_prev_stage<ExtractStage>();
    app.runner()
        .assign_loop_stage<RenderSubApp, RenderSubApp>(
            Prepare, PreRender, Render, PostRender
        )
        .add_prev_stage<ExtractStage>();
    app.runner().assign_exit_stage<MainSubApp, MainSubApp>(
        PreExit, Exit, PostExit
    );
    return std::move(app);
}
EPIX_API App App::create(const AppSettings& settings) {
    App app;
    app.runner().assign_startup_stage<MainSubApp, MainSubApp>(
        PreStartup, Startup, PostStartup
    );
    app.runner().assign_state_transition_stage<MainSubApp, MainSubApp>(Transit);
    if (settings.parrallel_rendering) {
        app.add_sub_app<RenderSubApp>();
        app.runner().assign_loop_stage<MainSubApp, RenderSubApp>(
            PreExtract, Extraction, PostExtract
        );
        app.runner()
            .assign_loop_stage<MainSubApp, MainSubApp>(
                First, PreUpdate, Update, PostUpdate, Last
            )
            .add_prev_stage<ExtractStage>();
        app.runner()
            .assign_loop_stage<RenderSubApp, RenderSubApp>(
                Prepare, PreRender, Render, PostRender
            )
            .add_prev_stage<ExtractStage>();
    } else {
        app.runner().assign_loop_stage<MainSubApp, MainSubApp>(
            First, PreUpdate, Update, PostUpdate, Last
        );
        app.runner()
            .assign_loop_stage<MainSubApp, MainSubApp>(
                Prepare, PreRender, Render, PostRender
            )
            .add_prev_stage<MainLoopStage>();
    }
    app.runner().assign_exit_stage<MainSubApp, MainSubApp>(
        PreExit, Exit, PostExit
    );
    return std::move(app);
}
EPIX_API void App::run() {
    m_logger->info("Building App");
    build();
    m_logger->info("Running App");
    m_logger->trace("Startup stage");
    m_runner->run_startup();
    end_commands();
    m_logger->trace("Transition stage");
    m_runner->run_state_transition();
    end_commands();
    do {
        FrameMark;
        update_states();
        m_logger->trace("Loop stage");
        m_runner->run_loop();
        tick_events();
        m_logger->trace("Transition stage");
        m_runner->run_state_transition();
        end_commands();
        {
            ZoneScopedN("bake runner");
            m_runner->bake_loop();
            m_runner->bake_state_transition();
        }
    } while (m_loop_enabled &&
             !m_check_exit_func->run(
                 m_sub_apps->at(std::type_index(typeid(MainSubApp))).get(),
                 m_sub_apps->at(std::type_index(typeid(MainSubApp))).get()
             ));
    update_states();
    m_logger->info("Exiting App");
    m_logger->trace("Exit stage");
    m_runner->run_exit();
    end_commands();
    m_logger->info("App terminated");
}
EPIX_API void App::set_log_level(spdlog::level::level_enum level) {
    m_logger->set_level(level);
    m_runner->set_log_level(level);
}
EPIX_API App& App::enable_loop() {
    m_loop_enabled = true;
    return *this;
}
EPIX_API App& App::disable_loop() {
    m_loop_enabled = false;
    return *this;
}
EPIX_API App* App::operator->() { return this; }

EPIX_API App::App()
    : m_sub_apps(std::make_unique<
                 entt::dense_map<std::type_index, std::unique_ptr<SubApp>>>()),
      m_runner(std::make_unique<Runner>(m_sub_apps.get())) {
    m_logger = spdlog::default_logger()->clone("app");
    m_check_exit_func =
        std::make_unique<BasicSystem<bool>>([](EventReader<AppExit> reader) {
            for (auto&& evt : reader.read()) {
                return true;
            }
            return false;
        });
    add_sub_app<MainSubApp>();
    add_event<AppExit>();
}
EPIX_API void App::build_plugins() {
    for (size_t i = 0; i < m_plugins.size(); i++) {
        auto [ptr, plugin] = m_plugins[i];
        m_logger->debug("Building plugin: {}", ptr.name());
        plugin->build(*this);
        for (auto&& [app_ptr, subapp] : *m_sub_apps) {
            subapp->m_world.m_resources.emplace(ptr, plugin);
        }
    }
}
EPIX_API void App::build() {
    build_plugins();
    m_runner->build();
    m_runner->bake_all();
}
EPIX_API void App::end_commands() {
    for (auto&& [ptr, subapp] : *m_sub_apps) {
        subapp->end_commands();
    }
}
EPIX_API void App::tick_events() {
    for (auto&& [ptr, subapp] : *m_sub_apps) {
        subapp->tick_events();
    }
}
EPIX_API void App::update_states() {
    for (auto&& [ptr, subapp] : *m_sub_apps) {
        subapp->update_states();
    }
}
EPIX_API Runner& App::runner() { return *m_runner; }