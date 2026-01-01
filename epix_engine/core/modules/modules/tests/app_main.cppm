module;

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

export module epix.core:tests.app_main;

import :app;

using namespace core;

struct TestRunner : public AppRunner {
    bool step(App& app) override {
        static int count = 0;
        spdlog::info("Step {}", count);
        count++;
        if (count >= 2) {
            return false;
        }
        app.update().get();
        return true;
    }
    void exit(App& app) override {
        spdlog::info("Exiting app.");
        app.run_schedules(PreExit, Exit, PostExit).get();
    }
};

export {
    TEST(core, app_main) {
        App app = App::create();
        app.add_systems(Startup, into([](Commands commands) {
                            spdlog::info("Hello from Startup system!");
                            commands.insert_resource(std::string("String Resource."));
                        }))
            .add_systems(Update, into([](std::optional<Res<std::string>> str_res) {
                             spdlog::info("Hello from Update system!");
                             spdlog::info("String Resource value: {}", *str_res.value());
                         }))
            .add_systems(Exit, into([]() { spdlog::info("Goodbye from Exit system!"); }));
        app.set_runner(std::make_unique<TestRunner>());
        app.run();
    }
}