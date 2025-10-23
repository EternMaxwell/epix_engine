#pragma once

#include <functional>
#include <optional>
#include <unordered_map>

#include "../schedule/schedule.hpp"

namespace epix::core::app {
/**
 * @brief A collection of schedules, used as resource in World.
 */
struct Schedules {
   public:
    Schedules() = default;

    std::optional<std::reference_wrapper<const schedule::Schedule>> get_schedule(
        const schedule::ScheduleLabel& label) const {
        auto it = _schedules.find(label);
        if (it != _schedules.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    const schedule::Schedule& schedule(const schedule::ScheduleLabel& label) const {
        return get_schedule(label).value();
    }
    std::optional<std::reference_wrapper<schedule::Schedule>> get_schedule_mut(const schedule::ScheduleLabel& label) {
        auto it = _schedules.find(label);
        if (it != _schedules.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    schedule::Schedule& schedule_mut(const schedule::ScheduleLabel& label) { return get_schedule_mut(label).value(); }
    schedule::Schedule& schedule_or_insert(schedule::Schedule&& schedule) {
        auto&& [it, inserted] = _schedules.emplace(schedule.label(), std::move(schedule));
        return it->second;
    }

    // adds a new schedule, or replaces the existing one with the same label
    schedule::Schedule& add_schedule(schedule::Schedule&& schedule) {
        if (auto it = _schedules.find(schedule.label()); it != _schedules.end()) {
            it->second = std::move(schedule);
            return it->second;
        } else {
            auto&& [it2, inserted] = _schedules.emplace(schedule.label(), std::move(schedule));
            return it2->second;
        }
    }

    std::optional<schedule::Schedule> remove_schedule(const schedule::ScheduleLabel& label) {
        auto it = _schedules.find(label);
        if (it != _schedules.end()) {
            schedule::Schedule schedule = std::move(it->second);
            _schedules.erase(it);
            return schedule;
        }
        return std::nullopt;
    }

   private:
    // schedule is movable, no need to use pointer
    std::unordered_map<schedule::ScheduleLabel, schedule::Schedule> _schedules;
};
}  // namespace epix::core::app