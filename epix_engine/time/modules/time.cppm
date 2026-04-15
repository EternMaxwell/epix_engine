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

/** @brief Strategy for how `Time<Real>` is updated each frame. */
export enum class TimeUpdateStrategy {
    Automatic,      /**< Use `steady_clock::now()` each frame. */
    ManualInstant,  /**< Advance to a caller-provided time_point. */
    ManualDuration, /**< Advance by a caller-provided duration. */
    FixedTimesteps, /**< Advance by `fixed_timestep * fixed_timestep_factor` each frame. */
};

/** @brief Configuration resource controlling how real time is updated.
 *  Set the strategy and associated field before the First schedule runs. */
export struct TimeUpdateConfig {
    TimeUpdateStrategy strategy = TimeUpdateStrategy::Automatic;
    /** @brief Time point used when strategy is ManualInstant. */
    std::chrono::steady_clock::time_point manual_instant{};
    /** @brief Duration used when strategy is ManualDuration. */
    std::chrono::nanoseconds manual_duration{0};
    /** @brief Multiplier for fixed timestep when strategy is FixedTimesteps. */
    std::uint32_t fixed_timestep_factor = 1;
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

/** @brief Plugin that registers all time resources and the FixedMain schedule.
 *  Adds `Time<>`, `Time<Real>`, `Time<Virtual>`, `Time<Fixed>`, `TimeUpdateConfig` resources,
 *  the real-time update system in First, and the FixedMain schedule with its sub-schedules. */
export struct TimePlugin {
    /** @brief Build the plugin into the app. */
    void build(App& app);
};

}  // namespace epix::time
