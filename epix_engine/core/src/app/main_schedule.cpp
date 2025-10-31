#include "epix/core/app.hpp"
#include "epix/core/app/app_sche.hpp"
#include "epix/core/app/state.hpp"
#include "epix/core/schedule/schedule.hpp"

namespace epix::core::app {
void MainSchedulePlugin::build(App& app) {
    auto schedules = std::array{
        schedule::Schedule(app::PreStartup).with_execute_config({.apply_direct = true, .run_once = true}),
        schedule::Schedule(app::Startup).with_execute_config({.apply_direct = true, .run_once = true}),
        schedule::Schedule(app::PostStartup).with_execute_config({.apply_direct = true, .run_once = true}),
        schedule::Schedule(app::First),
        schedule::Schedule(app::PreUpdate),
        schedule::Schedule(app::Update),
        schedule::Schedule(app::PostUpdate),
        schedule::Schedule(app::Last),
        schedule::Schedule(app::PreExit).with_execute_config({.run_once = true}),
        schedule::Schedule(app::Exit).with_execute_config({.run_once = true}),
        schedule::Schedule(app::PostExit).with_execute_config({.run_once = true}),
        schedule::Schedule(app::StateTransition).then([](schedule::Schedule& sche) {
            sche.configure_sets(schedule::make_sets(app::StateTransitionSet::Callback, app::StateTransitionSet::Transit).chain());
        }),
    };
    auto order = std::array{
        schedule::ScheduleLabel(app::PreStartup),  schedule::ScheduleLabel(app::Startup),
        schedule::ScheduleLabel(app::PostStartup), schedule::ScheduleLabel(app::First),
        schedule::ScheduleLabel(app::PreUpdate),   schedule::ScheduleLabel(app::StateTransition),
        schedule::ScheduleLabel(app::Update),      schedule::ScheduleLabel(app::PostUpdate),
        schedule::ScheduleLabel(app::Last),
    };
    std::ranges::for_each(schedules, [&app](schedule::Schedule& schedule) { app.add_schedule(std::move(schedule)); });
    app.schedule_order().insert_range_end(order);
}
}  // namespace epix::core::app