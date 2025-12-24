#include <spdlog/spdlog.h>

#include "epix/core/app/schedules.hpp"

namespace epix::core::app {
std::optional<std::reference_wrapper<const schedule::Schedule>> Schedules::get_schedule(
    const schedule::ScheduleLabel& label) const {
    auto it = _schedules.find(label);
    if (it != _schedules.end()) {
        return it->second;
    }
    return std::nullopt;
}
std::optional<std::reference_wrapper<schedule::Schedule>> Schedules::get_schedule_mut(
    const schedule::ScheduleLabel& label) {
    auto it = _schedules.find(label);
    if (it != _schedules.end()) {
        return it->second;
    }
    return std::nullopt;
}
schedule::Schedule& Schedules::add_schedule(schedule::Schedule&& schedule) {
    if (auto it = _schedules.find(schedule.label()); it != _schedules.end()) {
        spdlog::warn("Schedule '{}' already exists, it will be overwritten!", schedule.label().to_string());
        it->second = std::move(schedule);
        return it->second;
    } else {
        auto&& [it2, inserted] = _schedules.emplace(schedule.label(), std::move(schedule));
        return it2->second;
    }
}
std::optional<schedule::Schedule> Schedules::remove_schedule(const schedule::ScheduleLabel& label) {
    auto it = _schedules.find(label);
    if (it != _schedules.end()) {
        schedule::Schedule schedule = std::move(it->second);
        _schedules.erase(it);
        return schedule;
    }
    return std::nullopt;
}
}  // namespace epix::core::app
