module;

module epix.core;

import std;

import :app.main_schedule;
import :labels;

namespace epix::core {
void MainSchedulePlugin::build(App& app) {
    auto schedules = std::array{
        Schedule(PreStartup)
            .with_schedule_config({.executor_config = {.deferred = DeferredApply::ApplyDirect}, .run_once = true}),
        Schedule(Startup).with_schedule_config(
            {.executor_config = {.deferred = DeferredApply::ApplyDirect}, .run_once = true}),
        Schedule(PostStartup)
            .with_schedule_config({.executor_config = {.deferred = DeferredApply::ApplyDirect}, .run_once = true}),
        Schedule(First),
        Schedule(PreUpdate),
        Schedule(Update),
        Schedule(PostUpdate),
        Schedule(Last),
        Schedule(PreExit).with_schedule_config({.run_once = true}),
        Schedule(Exit).with_schedule_config({.run_once = true}),
        Schedule(PostExit).with_schedule_config({.run_once = true}),
        Schedule(StateTransition).then([](Schedule& sche) {
            sche.configure_sets(make_sets(StateTransitionSet::Transit, StateTransitionSet::Callback).chain());
        }),
    };
    auto order = std::array{
        ScheduleLabel(PreStartup), ScheduleLabel(Startup),    ScheduleLabel(PostStartup),
        ScheduleLabel(First),      ScheduleLabel(PreUpdate),  ScheduleLabel(StateTransition),
        ScheduleLabel(Update),     ScheduleLabel(PostUpdate), ScheduleLabel(Last),
    };
    std::ranges::for_each(schedules, [&app](Schedule& schedule) { app.add_schedule(std::move(schedule)); });
    app.schedule_order().insert_range_end(order);
}
}  // namespace core
