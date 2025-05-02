#include "epix/app/app.h"

using namespace epix::app;

EPIX_API void ScheduleGroup::build() noexcept {
    if (!needs_build) return;
    needs_build = false;
    for (auto&& [label, schedule] : schedules) {
        auto&& node = schedule_nodes[label];
        entt::dense_set<ScheduleLabel> not_presents;
        for (auto&& depend_label : node.depends) {
            auto&& it = schedules.find(depend_label);
            if (it != schedules.end()) {
                schedule_nodes.at(depend_label).succeeds.emplace(label);
            } else {
                not_presents.emplace(depend_label);
            }
        }
        for (auto&& not_present : not_presents) {
            node.depends.erase(not_present);
        }
        not_presents.clear();
        for (auto&& succeed_label : node.succeeds) {
            auto&& it = schedules.find(succeed_label);
            if (it != schedules.end()) {
                schedule_nodes.at(succeed_label).depends.emplace(label);
            } else {
                not_presents.emplace(succeed_label);
            }
        }
        for (auto&& not_present : not_presents) {
            node.succeeds.erase(not_present);
        }
        not_presents.clear();
    }
}

EPIX_API std::expected<void, RunGroupError> ScheduleGroup::run(App& app) {
    entt::dense_map<ScheduleLabel, size_t> set_depends_count;
    entt::dense_set<ScheduleLabel> schedules_running;
    std::deque<ScheduleLabel> wait_to_run;
    entt::dense_set<ScheduleLabel> finished_schedules;
    epix::utils::async::ConQueue<ScheduleLabel> just_finished_schedules;

    auto run_schedule = [&](ScheduleLabel label) {
        auto&& schedule = schedules.at(label);
        auto&& src      = schedule_src.at(label);
        auto&& dst      = schedule_dst.at(label);
        auto src_world  = app.get_world(src);
        auto dst_world  = app.get_world(dst);
        if (!src_world || !dst_world) {
            return;
        }
        ScheduleRunner* prunner;
        if (auto it = schedule_runners.find(label);
            it != schedule_runners.end()) {
            prunner = it->second.get();
        } else {
            prunner = schedule_runners
                          .emplace(
                              label,
                              std::make_unique<ScheduleRunner>(*schedule, false)
                          )
                          .first->second.get();
        }
        auto& runner = *prunner;
        runner.set_run_once(schedule_run_once.at(label));
        runner.get_tracy_settings().enabled =
            app.tracy_settings().schedule_enabled_tracy(label);
        runner.set_worlds(*src_world, *dst_world);
        runner.set_executors(app.get_executors());
        if (auto executor = app.get_control_pool()) {
            executor->detach_task([&, label]() {
                auto result = runner.run();
                runner.reset();
                just_finished_schedules.emplace(label);
            });
        } else {
            auto result = runner.run();
            runner.reset();
            just_finished_schedules.emplace(schedule->label);
        }
    };
    auto try_run_waiting = [&]() {
        size_t size = wait_to_run.size();
        for (size_t i = 0; i < size; i++) {
            ScheduleLabel label = wait_to_run.front();
            wait_to_run.pop_front();
            auto&& schedule = schedules.at(label);
            auto src_label  = schedule_src.at(label);
            auto dst_label  = schedule_dst.at(label);
            bool conflicts  = false;
            for (auto& running : schedules_running) {
                auto other_src = schedule_src.at(running);
                auto other_dst = schedule_dst.at(running);
                if (src_label == other_src && dst_label == other_dst) {
                    conflicts = true;
                    break;
                }
            }
            if (!conflicts) {
                schedules_running.emplace(label);
                run_schedule(label);
            } else {
                wait_to_run.emplace_back(label);
            }
        }
    };

    for (auto&& [label, schedule] : schedules) {
        set_depends_count.emplace(
            label, schedule_nodes.at(label).depends.size()
        );
        if (schedule_nodes.at(label).depends.empty()) {
            wait_to_run.emplace_back(label);
        }
    }

    try_run_waiting();
    while (!schedules_running.empty()) {
        auto finished_item = just_finished_schedules.pop();
        schedules_running.erase(finished_item);
        finished_schedules.emplace(finished_item);
        // decrease the count of depends of the succeeds of the finished
        for (auto&& succeed : schedule_nodes.at(finished_item).succeeds) {
            auto& depend_count = set_depends_count.at(succeed);
            depend_count--;
            if (depend_count == 0) {
                // all dependencies are finished, enter the set
                wait_to_run.emplace_back(succeed);
            }
        }
        try_run_waiting();
    }
    size_t remain_count = schedules.size() - finished_schedules.size();
    if (remain_count > 0) {
        return std::unexpected(RunGroupError{
            RunGroupError::ErrorType::ScheduleRemains, (uint32_t)remain_count
        });
    }
    return {};
}