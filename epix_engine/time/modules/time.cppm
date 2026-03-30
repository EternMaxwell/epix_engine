export module epix.time;

export import :time_clock;
export import :real;
export import :virt;
export import :fixed;
export import :stopwatch;
export import :timer;
export import :common_conditions;

import epix.core;
import std;

using namespace epix::core;

namespace epix::time {

export enum class TimeUpdateStrategy {
    Automatic,
    ManualInstant,
    ManualDuration,
    FixedTimesteps,
};

export struct TimeUpdateConfig {
    TimeUpdateStrategy strategy = TimeUpdateStrategy::Automatic;
    std::chrono::steady_clock::time_point manual_instant{};
    std::chrono::nanoseconds manual_duration{0};
    uint32_t fixed_timestep_factor = 1;
};

/** @brief Schedule for fixed-timestep systems. Loops based on accumulated time.
 *  Uses a custom executor that runs the sub-schedules below in order. */
export inline struct FixedMainT {
} FixedMain;
/** @brief Schedule: runs first in FixedMain each iteration. */
export inline struct FixedFirstT {
} FixedFirst;
/** @brief Schedule: runs before FixedUpdate in FixedMain. */
export inline struct FixedPreUpdateT {
} FixedPreUpdate;
/** @brief Schedule: main fixed-timestep update. */
export inline struct FixedUpdateT {
} FixedUpdate;
/** @brief Schedule: runs after FixedUpdate in FixedMain. */
export inline struct FixedPostUpdateT {
} FixedPostUpdate;
/** @brief Schedule: runs last in FixedMain each iteration. */
export inline struct FixedLastT {
} FixedLast;

export struct TimePlugin {
    void build(App& app);
};

}  // namespace epix::time
