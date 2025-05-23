#include "epix/utils/time.h"

using namespace epix::utils::time;

EPIX_API RepeatTimer::RepeatTimer(double interval)
    : interval(interval), left_time(0) {
    last_time = std::chrono::steady_clock::now();
}
EPIX_API uint32_t RepeatTimer::tick() {
    auto current = std::chrono::steady_clock::now();
    auto delta_time =
        std::chrono::duration<double, std::chrono::seconds::period>(
            current - last_time
        )
            .count();
    last_time = current;
    left_time += delta_time;
    if (left_time > interval) {
        uint32_t count = static_cast<uint32_t>(left_time / interval);
        left_time -= count * interval;
        return count;
    }
    return 0;
}

EPIX_API CountDownTimer::CountDownTimer(double time) : time(time) {
    start = std::chrono::steady_clock::now();
}
EPIX_API uint32_t CountDownTimer::tick() {
    auto current = std::chrono::steady_clock::now();
    auto delta_time =
        std::chrono::duration<double, std::chrono::seconds::period>(
            current - start
        )
            .count();
    if (delta_time > time) {
        return 1;
    }
    return 0;
}
EPIX_API void CountDownTimer::reset() {
    start = std::chrono::steady_clock::now();
}
EPIX_API void CountDownTimer::reset(double time) {
    this->time = time;
    start      = std::chrono::steady_clock::now();
}

EPIX_API Timer::Timer(Timer::Type t, double time)
    : type(t),
      interval(time),
      left(0),
      last_time(std::chrono::steady_clock::now()) {}

EPIX_API Timer Timer::repeat(double interval) {
    return Timer(Type::REPEAT, interval);
}
EPIX_API Timer Timer::once(double time) { return Timer(Type::ONCE, time); }
EPIX_API uint32_t Timer::tick() {
    auto current = pause_time ? *pause_time : std::chrono::steady_clock::now();
    auto delta_time =
        std::chrono::duration<double, std::chrono::seconds::period>(
            current - last_time
        )
            .count();
    uint32_t count = 0;
    switch (type) {
        case Type::ONCE:
            count = delta_time > interval ? 1 : 0;
            break;
        case Type::REPEAT:
            left += delta_time;
            last_time = current;
            if (left > interval) {
                count = static_cast<uint32_t>(left / interval);
                left -= count * interval;
            }
            break;

        default:
            break;
    }
    return count;
}

EPIX_API void Timer::reset() {
    last_time  = std::chrono::steady_clock::now();
    left       = 0;
    pause_time = std::nullopt;
}
EPIX_API void Timer::reset(double v) {
    reset();
    interval = v;
}

EPIX_API void Timer::pause() { pause_time = std::chrono::steady_clock::now(); }
EPIX_API void Timer::unpause() {
    if (pause_time) {
        auto current = std::chrono::steady_clock::now();
        last_time += current - *pause_time;
    }
    pause_time = std::nullopt;
}