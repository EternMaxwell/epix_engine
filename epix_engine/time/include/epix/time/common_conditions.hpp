#pragma once

#include <chrono>

#include <epix/time/time_clock.hpp>
#include <epix/time/real.hpp>
#include <epix/time/virt.hpp>
#include <epix/time/timer.hpp>

#include <epix/core.hpp>


namespace epix::time {
using namespace epix::core;

/** @brief Run condition that fires periodically based on virtual (`Time<>`) time.
 *  Returns a system-compatible lambda that ticks an internal repeating timer
 *  and returns true each time the timer completes a cycle. */
inline auto on_timer(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Repeating)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires periodically based on real (`Time<Real>`) time.
 *  Unaffected by pause or speed changes. */
inline auto on_real_timer(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Repeating)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires exactly once after a delay in virtual time.
 *  Returns true on the tick when the delay elapses, then never again. */
inline auto once_after_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires exactly once after a delay in real time.
 *  Returns true on the tick when the delay elapses, then never again. */
inline auto once_after_real_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that returns false until a delay elapses in virtual time,
 *  then returns true every tick thereafter. */
inline auto repeating_after_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

/** @brief Run condition that returns false until a delay elapses in real time,
 *  then returns true every tick thereafter. */
inline auto repeating_after_real_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

/** @brief Run condition that returns true when virtual time is paused. */
inline bool paused(Res<Time<Virtual>> time) noexcept { return time->is_paused(); }

}  // namespace epix::time
