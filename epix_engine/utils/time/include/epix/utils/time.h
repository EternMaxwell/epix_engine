#pragma once

#include <epix/common.h>

#include <chrono>

namespace epix::utils::time {
struct RepeatTimer {
   private:
    double interval;   // delta time between 2 ticks
    double left_time;  // last tick time
    std::chrono::time_point<std::chrono::steady_clock> last_time;

   public:
    EPIX_API RepeatTimer(double interval);
    EPIX_API uint32_t tick();
};
struct CountDownTimer {
   private:
    double time;
    std::chrono::time_point<std::chrono::steady_clock> start;

   public:
    EPIX_API CountDownTimer(double time);
    EPIX_API uint32_t tick();
    EPIX_API void reset();
    EPIX_API void reset(double time);
};

struct Timer {
   private:
    enum class Type { ONCE, REPEAT } type;
    double interval;
    double left;
    std::chrono::time_point<std::chrono::steady_clock> last_time;
    std::optional<std::chrono::time_point<std::chrono::steady_clock>>
        pause_time;

    EPIX_API Timer(Type, double);

   public:
    /**
     * @brief Create a repeat timer with the given interval in seconds.
     */
    EPIX_API static Timer repeat(double interval);
    /**
     * @brief Create a once timer with the given count down time in seconds;
     */
    EPIX_API static Timer once(double time);

    /**
     * @brief Tick the timer and get how many ticks has passed since last call.
     * Or 1 if time exceeded and 0 if time not exceeded.
     *
     * @return A `uint32_t` indicating how many ticks has passed or one of 1 or
     * 0 when this is a once timer.
     */
    EPIX_API uint32_t tick();
    /**
     * @brief Reset timer. For Once timer, re-count down. For Repeat timer,
     * re-counting and abandon previously left delta time.
     */
    EPIX_API void reset();
    /**
     * @brief Same as `reset()` but letting the caller to set new interval time
     * for Repeat timer or new count time for once timer.
     */
    EPIX_API void reset(double);

    EPIX_API void pause();
    EPIX_API void unpause();
};
}  // namespace epix::utils::time