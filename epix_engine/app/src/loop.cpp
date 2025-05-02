#include "epix/app/app.h"

using namespace epix::app;

struct LoopRunner : public AppRunner {
    BasicSystem<bool> m_check_exit;
    LoopRunner()
        : m_check_exit([](EventReader<AppExit> exit) {
              if (auto e = exit.read_one()) {
                  return true;
              }
              return false;
          }) {}
    int run(App& app) override {
        do {
            app.run_group(LoopGroup);
            if (app.tracy_settings().mark_frame) {
                FrameMark;
            }
        } while (!app.run_system(m_check_exit).value_or(true));
        app.logger()->clone("loop")->info("Received exit event.");
        app.run_group(ExitGroup);
        app.logger()->info("App terminated.");
        return 0;
    }
};

EPIX_API void LoopPlugin::build(App& app) {
    app.set_runner(std::make_shared<LoopRunner>());
    app.add_events<AppExit>();
}
EPIX_API LoopPlugin& LoopPlugin::set_enabled(bool enabled) {
    m_loop_enabled = enabled;
    return *this;
}