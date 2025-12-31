module;

// #include <spdlog/spdlog.h>

module epix.core;

import :app.schedules;
import :labels;

namespace core {
std::optional<std::reference_wrapper<const Schedule>> Schedules::get_schedule(const ScheduleLabel& label) const {
    auto it = _schedules.find(label);
    if (it != _schedules.end()) {
        return it->second;
    }
    return std::nullopt;
}
std::optional<std::reference_wrapper<Schedule>> Schedules::get_schedule_mut(const ScheduleLabel& label) {
    auto it = _schedules.find(label);
    if (it != _schedules.end()) {
        return it->second;
    }
    return std::nullopt;
}
Schedule& Schedules::add_schedule(Schedule&& schedule) {
    if (auto it = _schedules.find(schedule.label()); it != _schedules.end()) {
        std::println(std::cerr, "Schedule '{}' already exists, it will be overwritten!", schedule.label().to_string());
        it->second = std::move(schedule);
        return it->second;
    } else {
        auto&& [it2, inserted] = _schedules.emplace(schedule.label(), std::move(schedule));
        return it2->second;
    }
}
std::optional<Schedule> Schedules::remove_schedule(const ScheduleLabel& label) {
    auto it = _schedules.find(label);
    if (it != _schedules.end()) {
        Schedule schedule = std::move(it->second);
        _schedules.erase(it);
        return schedule;
    }
    return std::nullopt;
}
}  // namespace core
