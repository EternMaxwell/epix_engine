#include "epix/app/app.h"

using namespace epix::app;

struct LoopRunner : public AppRunner {
    LoopRunner() = default;
    int run(App& app) override {
        int code;
        std::unique_ptr<BasicSystem<bool>> m_check_exit =
            IntoSystem::into_system([&code](EventReader<AppExit> exit) {
                if (auto e = exit.read_one()) {
                    code = e->code;
                    return true;
                }
                return false;
            });
        do {
            app.update().get();
        } while (!app.run_system(m_check_exit.get()).value_or(true));
        spdlog::info("[loop] Received exit event : code = {}", code);
        spdlog::info("[app] Exiting app.");
        app.exit().get();
        spdlog::info("[app] App terminated.");
        return 0;
    }
};

EPIX_API void LoopPlugin::build(App& app) {
    app.set_runner(std::make_unique<LoopRunner>());
}
EPIX_API LoopPlugin& LoopPlugin::set_enabled(bool enabled) {
    m_loop_enabled = enabled;
    return *this;
}