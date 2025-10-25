#include "epix/core/app.hpp"

using namespace epix;
using namespace epix::core;
using namespace epix::core::app;

int main() {
    App app = App::create();
    app.add_systems(Startup, into([]() { spdlog::info("Hello from Startup system!"); }))
        .add_systems(Update, into([]() { spdlog::info("Hello from Update system!"); }))
        .add_systems(Exit, into([]() { spdlog::info("Goodbye from Exit system!"); }));
    app.set_runner([](App& app) {
        spdlog::info("[app] Calling update...");
        app.update().get();
        spdlog::info("[app] Calling update again...");
        app.update().get();
        spdlog::info("[app] Exiting app.");
        app.run_schedules(app::PreExit, app::Exit, app::PostExit);
        return 0;
    });
    app.run();
}