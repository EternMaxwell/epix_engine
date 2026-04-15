module;

#include <spdlog/spdlog.h>

module epix.time;

import epix.meta;
import epix.core;
import std;

using namespace epix::core;
namespace epix::time {

/** @brief Custom executor for FixedMain that runs the fixed sub-schedules
 *  (FixedFirst, FixedPreUpdate, FixedUpdate, FixedPostUpdate, FixedLast) in order,
 *  ignoring FixedMain's own systems. */
struct FixedMainExecutor : ScheduleExecutor {
    std::array<ScheduleLabel, 5> sub_schedules = {
        ScheduleLabel(FixedFirst),      ScheduleLabel(FixedPreUpdate), ScheduleLabel(FixedUpdate),
        ScheduleLabel(FixedPostUpdate), ScheduleLabel(FixedLast),
    };

    void execute(ScheduleSystems& /*unused*/, World& world, const ExecutorConfig& /*unused*/) override {
        auto& schedules = world.resource_mut<Schedules>();
        for (auto& label : sub_schedules) {
            auto schedule = schedules.remove_schedule(label);
            if (schedule) {
                schedule->execute(world);
                schedules.add_schedule(std::move(*schedule));
            }
        }
    }
    meta::type_index type() const override { return meta::type_id<FixedMainExecutor>(); }
};

void TimePlugin::build(App& app) {
    spdlog::debug("[time] Building TimePlugin.");
    app.world_mut().init_resource<Time<>>();
    app.world_mut().init_resource<Time<Real>>();
    app.world_mut().init_resource<Time<Virtual>>();
    app.world_mut().init_resource<Time<Fixed>>();
    app.world_mut().init_resource<TimeUpdateConfig>();

    app.add_systems(
        First,
        into([](ResMut<Time<Real>> real_time, ResMut<Time<Virtual>> virtual_time, Res<Time<Fixed>> fixed_time,
                ResMut<Time<>> time, Res<TimeUpdateConfig> update_strategy) {
            switch (update_strategy->strategy) {
                case TimeUpdateStrategy::Automatic:
                    real_time->update();
                    break;
                case TimeUpdateStrategy::ManualInstant:
                    real_time->update_with_instant(update_strategy->manual_instant);
                    break;
                case TimeUpdateStrategy::ManualDuration:
                    real_time->update_with_duration(update_strategy->manual_duration);
                    break;
                case TimeUpdateStrategy::FixedTimesteps:
                    real_time->update_with_duration(fixed_time->timestep() * update_strategy->fixed_timestep_factor);
                    break;
            }
            Time<>& current = *time;
            update_virtual_time(current, *virtual_time, *real_time);
        }).set_name("time_system"));

    // Register sub-schedules for fixed-timestep execution
    app.add_schedule(Schedule(FixedFirst));
    app.add_schedule(Schedule(FixedPreUpdate));
    app.add_schedule(Schedule(FixedUpdate));
    app.add_schedule(Schedule(FixedPostUpdate));
    app.add_schedule(Schedule(FixedLast));

    // Register FixedMain schedule with loop condition and custom executor
    ScheduleConfig fixed_config;
    fixed_config.loop_condition = [](World& world) -> bool {
        auto& fixed_time = world.resource_mut<Time<Fixed>>();
        if (!fixed_time.expend()) return false;
        world.resource_mut<Time<>>() = fixed_time.as_generic();
        return true;
    };

    app.add_schedule(Schedule(FixedMain)
                         .with_schedule_config(fixed_config)
                         .with_executor(std::make_unique<FixedMainExecutor>())
                         .then([](Schedule& sche) {
                             sche.add_pre_systems(
                                 into([](ResMut<Time<Fixed>> fixed_time, Res<Time<Virtual>> virtual_time) {
                                     fixed_time->accumulate_overstep(virtual_time->delta());
                                 }).set_name("accumulate_fixed_overstep"));
                             sche.add_post_systems(into([](ResMut<Time<>> time, Res<Time<Virtual>> virtual_time) {
                                                       *time = virtual_time->as_generic();
                                                   }).set_name("restore_virtual_time"));
                         }));

    // Insert FixedMain into schedule order after StateTransition (before Update)
    app.schedule_order().insert_after(ScheduleLabel(StateTransition), ScheduleLabel(FixedMain));
}

}  // namespace epix::time
