module;

#ifndef EPIX_IMPORT_STD
#include <functional>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#endif
export module epix.core:app.schedules;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :schedule;

namespace epix::core {
/**
 * @brief A collection of schedules, keyed by ScheduleLabel.
 *  Stored as a resource in World; provides lookup, insertion and removal.
 */
export struct Schedules {
   public:
    Schedules()                            = default;
    Schedules(const Schedules&)            = delete;
    Schedules(Schedules&&)                 = default;
    Schedules& operator=(const Schedules&) = delete;
    Schedules& operator=(Schedules&&)      = default;

    /** @brief Try get const reference to a schedule by its label. */
    std::optional<std::reference_wrapper<const Schedule>> get_schedule(const ScheduleLabel& label) const;
    /** @brief Get a const reference to a schedule by its label, throws if not found. */
    const Schedule& schedule(const ScheduleLabel& label) const { return get_schedule(label).value(); }
    /** @brief Try get mutable reference to a schedule by its label. */
    std::optional<std::reference_wrapper<Schedule>> get_schedule_mut(const ScheduleLabel& label);
    /** @brief Get a mutable reference to a schedule by its label, throws if not found. */
    Schedule& schedule_mut(const ScheduleLabel& label) { return get_schedule_mut(label).value(); }
    /** @brief Get or insert a schedule by its label. */
    Schedule& schedule_or_insert(Schedule&& schedule) {
        auto&& [it, inserted] = _schedules.try_emplace(schedule.label(), std::move(schedule));
        return it->second;
    }
    /** @brief Add a new schedule, or replace the existing one with the same label. */
    Schedule& add_schedule(Schedule&& schedule);
    /** @brief Remove a schedule by its label.
     *  @return The removed schedule if it existed, std::nullopt otherwise. */
    std::optional<Schedule> remove_schedule(const ScheduleLabel& label);

    /** @brief Iterate over all schedules with const access. */
    auto iter() const { return std::views::all(_schedules); }
    /** @brief Iterate over all schedules with mutable access. */
    auto iter_mut() { return std::views::all(_schedules); }

    /** @brief Propagate tick to all schedules for change detection. */
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