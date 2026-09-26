#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/app.hpp>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

using namespace epix::app;
using namespace epix::ecs;

namespace {
std::vector<std::string> actions;

struct TestRunner : public AppRunner {
    bool step(App& app) override {
        static int count = 0;
        if (count >= 2) {
            return false;
        }
        spdlog::info("Step {}", count);
        actions.emplace_back(std::format("step-{}", count));
        count++;
        app.update();
        return true;
    }
    void exit(App& app) override {
        spdlog::info("Exiting app.");
        app.run_schedules(PreExit, Exit, PostExit);
    }
};
struct AppTracked {};
}  // namespace

TEST(core, app_main) {
    auto level = spdlog::get_level();
    spdlog::set_level(spdlog::level::off);

    actions.clear();
    App app = App::create();
    app.add_systems(Startup, into([](Commands commands) {
                        spdlog::info("Hello from Startup system!");
                        actions.emplace_back("startup");
                        commands.insert_resource(std::string("String Resource."));
                    }))
        .add_systems(Update, into([](std::optional<Res<std::string>> str_res) {
                         spdlog::info("Hello from Update system!");
                         actions.emplace_back("update");
                         spdlog::info("String Resource value: {}", *str_res.value());
                     }))
        .add_systems(Exit, into([]() {
                         spdlog::info("Goodbye from Exit system!");
                         actions.emplace_back("exit");
                     }));
    app.set_runner(std::make_unique<TestRunner>());
    app.run();
    std::vector<std::string> expected = {"step-0", "startup", "update", "step-1", "update", "exit"};

    spdlog::set_level(level);

    ASSERT_EQ(actions, expected);
}

TEST(core, app_updates_clear_removed_component_trackers_on_the_next_boundary) {
    App app;
    Entity entity = app.world_mut().spawn(AppTracked{}).id();
    app.world_mut().entity_mut(entity).remove<AppTracked>();

    app.update();
    EXPECT_FALSE(std::ranges::empty(app.world().removed<AppTracked>()));

    app.update();
    EXPECT_TRUE(std::ranges::empty(app.world().removed<AppTracked>()));
}
