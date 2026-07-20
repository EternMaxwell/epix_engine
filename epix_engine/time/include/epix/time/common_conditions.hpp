#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <chrono>
#include <epix/ecs.hpp>
#endif

#include <epix/time/real.hpp>
#include <epix/time/time_clock.hpp>
#include <epix/time/timer.hpp>
#include <epix/time/virt.hpp>

namespace epix::time {

/** @brief Run condition that fires periodically based on virtual (`Time<>`) time.
 *  Returns a system-compatible lambda that ticks an internal repeating timer
 *  and returns true each time the timer completes a cycle. */
EPIX_EXPORT inline auto on_timer(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Repeating)](epix::ecs::Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires periodically based on real (`Time<Real>`) time.
 *  Unaffected by pause or speed changes. */
EPIX_EXPORT inline auto on_real_timer(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Repeating)](epix::ecs::Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires exactly once after a delay in virtual time.
 *  Returns true on the tick when the delay elapses, then never again. */
EPIX_EXPORT inline auto once_after_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](epix::ecs::Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires exactly once after a delay in real time.
 *  Returns true on the tick when the delay elapses, then never again. */
EPIX_EXPORT inline auto once_after_real_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](epix::ecs::Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that returns false until a delay elapses in virtual time,
 *  then returns true every tick thereafter. */
EPIX_EXPORT inline auto repeating_after_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](epix::ecs::Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

/** @brief Run condition that returns false until a delay elapses in real time,
 *  then returns true every tick thereafter. */
EPIX_EXPORT inline auto repeating_after_real_delay(std::chrono::nanoseconds duration) noexcept {
    return [timer = Timer(duration, TimerMode::Once)](epix::ecs::Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

/** @brief Run condition that returns true when virtual time is paused. */
EPIX_EXPORT inline bool paused(epix::ecs::Res<Time<Virtual>> time) noexcept { return time->is_paused(); }

}  // namespace epix::time
