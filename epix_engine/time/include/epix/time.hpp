#pragma once

#include <cstdint>
#include <chrono>

#include <epix/time/time_clock.hpp>
#include <epix/time/real.hpp>
#include <epix/time/virt.hpp>
#include <epix/time/fixed.hpp>
#include <epix/time/stopwatch.hpp>
#include <epix/time/timer.hpp>
#include <epix/time/common_conditions.hpp>

#include <epix/core.hpp>

namespace epix::time {
using namespace epix::core;

/** @brief Strategy for how `Time<Real>` is updated each frame. */
enum class TimeUpdateStrategy {
    Automatic,      /**< Use `steady_clock::now()` each frame. */
    ManualInstant,  /**< Advance to a caller-provided time_point. */
    ManualDuration, /**< Advance by a caller-provided duration. */
    FixedTimesteps, /**< Advance by `fixed_timestep * fixed_timestep_factor` each frame. */
};

/** @brief Configuration resource controlling how real time is updated.
 *  Set the strategy and associated field before the First schedule runs. */
struct TimeUpdateConfig {
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
inline struct FixedMainT {
} FixedMain;
/** @brief Schedule: runs first in FixedMain each iteration. */
inline struct FixedFirstT {
} FixedFirst;
/** @brief Schedule: runs before FixedUpdate in FixedMain. */
inline struct FixedPreUpdateT {
} FixedPreUpdate;
/** @brief Schedule: main fixed-timestep update. */
inline struct FixedUpdateT {
} FixedUpdate;
/** @brief Schedule: runs after FixedUpdate in FixedMain. */
inline struct FixedPostUpdateT {
} FixedPostUpdate;
/** @brief Schedule: runs last in FixedMain each iteration. */
inline struct FixedLastT {
} FixedLast;

/** @brief Plugin that registers all time resources and the FixedMain schedule.
 *  Adds `Time<>`, `Time<Real>`, `Time<Virtual>`, `Time<Fixed>`, `TimeUpdateConfig` resources,
 *  the real-time update system in First, and the FixedMain schedule with its sub-schedules. */
struct TimePlugin {
    /** @brief Build the plugin into the app. */
    void attach(App& app);
};

}  // namespace epix::time
