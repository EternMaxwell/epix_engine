module;

#include <memory>

module epix.core;

import :app.loop;
import :app.main_schedule;
import :system;
import :query;

namespace core {
struct LoopRunner : public AppRunner {
    std::unique_ptr<System<std::tuple<>, bool>> check_exit;
    FilteredAccessSet access;
    LoopRunner(App& app) {
        check_exit = make_system_unique([](EventReader<AppExit> exits) {
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
    void exit(App& app) override { app.run_schedules(PreExit, Exit, PostExit); }
};
void LoopPlugin::build(App& app) {
    app.add_event<AppExit>();
    auto check_exit = make_system_unique([](EventReader<AppExit> exits) {
        if (!exits.empty()) {
            return true;
        }
        return false;
    });
    auto access     = check_exit->initialize(app.world_mut());
    app.set_runner(std::make_unique<LoopRunner>(app));
}
}  // namespace core