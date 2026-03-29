module epix.time;

import epix.core;
import std;

using namespace core;

void time::TimePlugin::build(App& app) {
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

    // Register FixedMain schedule with loop condition
    ScheduleConfig fixed_config;
    fixed_config.loop_condition = [](World& world) -> bool {
        auto& fixed_time = world.resource_mut<Time<Fixed>>();
        if (!fixed_time.expend()) return false;
        world.resource_mut<Time<>>() = fixed_time.as_generic();
        return true;
    };

    app.add_schedule(Schedule(FixedMain).with_schedule_config(fixed_config).then([](Schedule& sche) {
        sche.add_pre_systems(into([](ResMut<Time<Fixed>> fixed_time, Res<Time<Virtual>> virtual_time) {
                                 fixed_time->accumulate_overstep(virtual_time->delta());
                             }).set_name("accumulate_fixed_overstep"));
        sche.add_post_systems(into([](ResMut<Time<>> time, Res<Time<Virtual>> virtual_time) {
                                  *time = virtual_time->as_generic();
                              }).set_name("restore_virtual_time"));
        sche.configure_sets(sets(FixedFirst, FixedPreUpdate, FixedUpdate, FixedPostUpdate, FixedLast).chain());
    }));

    // Insert FixedMain into schedule order after StateTransition (before Update)
    app.schedule_order().insert_after(ScheduleLabel(StateTransition), ScheduleLabel(FixedMain));
}
