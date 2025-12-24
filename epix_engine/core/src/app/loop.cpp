#include "epix/core/app.hpp"
#include "epix/core/app/loop.hpp"
#include "epix/core/event/reader.hpp"

namespace epix::core::app {
struct LoopRunner : public AppRunner {
    std::unique_ptr<system::System<std::tuple<>, bool>> check_exit;
    core::query::FilteredAccessSet access;
    LoopRunner(App& app) {
        check_exit = system::make_system_unique([](event::EventReader<AppExit> exits) {
            if (!exits.empty()) {
                return true;
            }
            return false;
        });
        access     = check_exit->initialize(app.world_mut());
    }
    bool step(App& app) override {
        app.update().wait();
        return !app.system_dispatcher()->dispatch_system(*check_exit, {}, access).get().value_or(false);
    }
    void exit(App& app) override { app.run_schedules(app::PreExit, app::Exit, app::PostExit); }
};
void LoopPlugin::build(App& app) {
    app.add_event<AppExit>();
    auto check_exit = system::make_system_unique([](event::EventReader<AppExit> exits) {
        if (!exits.empty()) {
            return true;
        }
        return false;
    });
    auto access     = check_exit->initialize(app.world_mut());
    app.set_runner(std::make_unique<LoopRunner>(app));
}
}  // namespace epix::core::app