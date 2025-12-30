module;

#include <functional>
#include <optional>
#include <ranges>
#include <unordered_map>

export module epix.core:app.schedules;

import :schedule;

namespace core {
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
    std::optional<std::reference_wrapper<const Schedule>> get_schedule(const ScheduleLabel& label) const;
    /// Get a const reference to a schedule by its label, throws if not found.
    const Schedule& schedule(const ScheduleLabel& label) const { return get_schedule(label).value(); }
    /// Try get mutable reference to a schedule by its label.
    std::optional<std::reference_wrapper<Schedule>> get_schedule_mut(const ScheduleLabel& label);
    /// Get a mutable reference to a schedule by its label, throws if not found.
    Schedule& schedule_mut(const ScheduleLabel& label) { return get_schedule_mut(label).value(); }
    /// Get or insert a schedule by its label.
    Schedule& schedule_or_insert(Schedule&& schedule) {
        auto&& [it, inserted] = _schedules.try_emplace(schedule.label(), std::move(schedule));
        return it->second;
    }
    /// Add a new schedule, or replaces the existing one with the same label
    Schedule& add_schedule(Schedule&& schedule);
    /// Remove a schedule by its label, returns the removed schedule if found.
    std::optional<Schedule> remove_schedule(const ScheduleLabel& label);

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
    std::unordered_map<ScheduleLabel, Schedule> _schedules;
};
}  // namespace core