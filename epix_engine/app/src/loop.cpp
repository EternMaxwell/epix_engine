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
        auto time_line1 = std::chrono::high_resolution_clock::now();
        do {
            app.run_group(LoopGroup);
            if (app.tracy_settings().mark_frame) {
                FrameMark;
            }
            auto time_line2 = std::chrono::high_resolution_clock::now();
            double time     = std::chrono::duration_cast<
                                  std::chrono::duration<double, std::milli>>(
                              time_line2 - time_line1
            )
                              .count();
            time_line1 = time_line2;
            if (auto app_profiler =
                    app.world(MainWorld).get_resource<AppProfiler>()) {
                app_profiler->push_time(time);
            }
        } while (!app.run_system(m_check_exit).value_or(true));
        app.logger()->clone("loop")->info("Received exit event.");
        app.logger()->info("Exiting app.");
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