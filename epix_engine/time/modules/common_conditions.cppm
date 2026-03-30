export module epix.time:common_conditions;

import std;

import :time_clock;
import :real;
import :virt;
import :timer;

import epix.core;

using namespace epix::core;

namespace epix::time {

/** @brief Run condition that fires periodically based on virtual (`Time<>`) time.
 *  Returns a system-compatible lambda that ticks an internal repeating timer
 *  and returns true each time the timer completes a cycle. */
export inline auto on_timer(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Repeating)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires periodically based on real (`Time<Real>`) time.
 *  Unaffected by pause or speed changes. */
export inline auto on_real_timer(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Repeating)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires exactly once after a delay in virtual time.
 *  Returns true on the tick when the delay elapses, then never again. */
export inline auto once_after_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that fires exactly once after a delay in real time.
 *  Returns true on the tick when the delay elapses, then never again. */
export inline auto once_after_real_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

/** @brief Run condition that returns false until a delay elapses in virtual time,
 *  then returns true every tick thereafter. */
export inline auto repeating_after_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

/** @brief Run condition that returns false until a delay elapses in real time,
 *  then returns true every tick thereafter. */
export inline auto repeating_after_real_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

/** @brief Run condition that returns true when virtual time is paused. */
export inline bool paused(Res<Time<Virtual>> time) { return time->is_paused(); }

}  // namespace epix::time
