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
    Schedules()                            = default;
    Schedules(const Schedules&)            = delete;
    Schedules(Schedules&&)                 = default;
    Schedules& operator=(const Schedules&) = delete;
    Schedules& operator=(Schedules&&)      = default;

    /// Try get const reference to a schedule by its label.
    std::optional<std::reference_wrapper<const schedule::Schedule>> get_schedule(
        const schedule::ScheduleLabel& label) const;
    /// Get a const reference to a schedule by its label, throws if not found.
    const schedule::Schedule& schedule(const schedule::ScheduleLabel& label) const {
        return get_schedule(label).value();
    }
    /// Try get mutable reference to a schedule by its label.
    std::optional<std::reference_wrapper<schedule::Schedule>> get_schedule_mut(const schedule::ScheduleLabel& label);
    /// Get a mutable reference to a schedule by its label, throws if not found.
    schedule::Schedule& schedule_mut(const schedule::ScheduleLabel& label) { return get_schedule_mut(label).value(); }
    /// Get or insert a schedule by its label.
    schedule::Schedule& schedule_or_insert(schedule::Schedule&& schedule) {
        auto&& [it, inserted] = _schedules.try_emplace(schedule.label(), std::move(schedule));
        return it->second;
    }
    /// Add a new schedule, or replaces the existing one with the same label
    schedule::Schedule& add_schedule(schedule::Schedule&& schedule);
    /// Remove a schedule by its label, returns the removed schedule if found.
    std::optional<schedule::Schedule> remove_schedule(const schedule::ScheduleLabel& label);

    /// Iterate over all schedules with const access
    auto iter() const { return std::views::all(_schedules); }
    /// Iterate over all schedules with mutable access
    auto iter_mut() { return std::views::all(_schedules); }

    void check_change_tick(Tick tick) {
        for (auto&& [_, schedule] : _schedules) {
            schedule.check_change_tick(tick);
        }
    }

   private:
    // schedule is movable, no need to use pointer
    std::unordered_map<schedule::ScheduleLabel, schedule::Schedule> _schedules;

    // weird, copy_constructible should fail, but it passes?
    // static_assert(std::copy_constructible<std::unordered_map<schedule::ScheduleLabel, schedule::Schedule>>);
};
}  // namespace epix::core::app