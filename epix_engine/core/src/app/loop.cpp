#include "epix/core/app.hpp"
#include "epix/core/app/loop.hpp"
#include "epix/core/event/reader.hpp"

namespace epix::core::app {
void LoopPlugin::build(App& app) {
    app.add_event<AppExit>();
    auto check_exit = system::make_system_unique([](event::EventReader<AppExit> exits) {
        if (!exits.empty()) {
            return true;
        }
        return false;
    });
    auto access     = check_exit->initialize(app.world_mut());
    app.set_runner([access = std::move(access), check_exit = std::move(check_exit)](App& app) {
        while (!app.system_dispatcher()->dispatch_system(*check_exit, {}, access).get().value_or(false)) {
            app.update().wait();
        }
        spdlog::info("[app] Exiting app.");
        app.run_schedules(app::PreExit, app::Exit, app::PostExit).wait();
    });
}
}  // namespace epix::core::app