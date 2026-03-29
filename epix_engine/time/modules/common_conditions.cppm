export module epix.time:common_conditions;

import std;

import :time_clock;
import :real;
import :virt;
import :timer;

import epix.core;

using namespace core;

namespace time {

export inline auto on_timer(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Repeating)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

export inline auto on_real_timer(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Repeating)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

export inline auto once_after_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

export inline auto once_after_real_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.just_finished();
    };
}

export inline auto repeating_after_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

export inline auto repeating_after_real_delay(std::chrono::nanoseconds duration) {
    return [timer = Timer(duration, TimerMode::Once)](Res<Time<Real>> time) mutable -> bool {
        timer.tick(time->delta());
        return timer.is_finished();
    };
}

export inline bool paused(Res<Time<Virtual>> time) { return time->is_paused(); }

}  // namespace time
