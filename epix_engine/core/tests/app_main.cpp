#include "epix/core.hpp"
#include "epix/core/app.hpp"

using namespace epix;
using namespace epix::core;
using namespace epix::core::app;

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
        app.run_schedules(app::PreExit, app::Exit, app::PostExit).get();
    }
};

int main() {
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