#include <spdlog/spdlog.h>

#include <epix/core/app.hpp>
#include <epix/core/app/loop.hpp>
#include <epix/core/app/main_schedule.hpp>
#include <epix/core/query.hpp>
#include <epix/core/system.hpp>

namespace epix::core {
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
        app.update();
        bool should_exit = false;
        app.world_scope([&](World& world) {
            auto res = check_exit->run({}, world);
            if (res.has_value()) should_exit = res.value();
        });
        if (should_exit) spdlog::debug("[app.loop] AppExit event received, loop will terminate.");
        return !should_exit;
    }
    void exit(App& app) override {
        spdlog::debug("[app.loop] LoopRunner exiting, running exit schedules.");
        app.run_schedules(PreExit, Exit, PostExit);
    }
};
void LoopPlugin::attach(App& app) {
    spdlog::debug("[app] Attaching LoopPlugin.");
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
}  // namespace epix::core
