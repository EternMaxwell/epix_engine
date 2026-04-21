module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstdint>
#include <limits>
#include <chrono>
#endif

export module epix.time:timer;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :stopwatch;

namespace epix::time {

/** @brief Whether a timer fires once or repeats. */
export enum class TimerMode {
    Once,      /**< Fire once; once finished, no further ticks have effect. */
    Repeating, /**< Automatically reset and re-fire when elapsed exceeds duration. */
};

/** @brief A countdown timer built on a Stopwatch. Supports one-shot and repeating modes.
 *  Call `tick()` each frame with the delta time to advance the timer. */
export struct Timer {
    Timer() = default;

    /** @brief Construct with a duration and mode. */
    Timer(std::chrono::nanoseconds duration, TimerMode mode) : m_duration(duration), m_mode(mode) {}

    /** @brief Construct from a duration in float seconds and a mode. */
    static Timer from_seconds(float duration, TimerMode mode) {
        return Timer(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(duration)),
                     mode);
    }

    /** @brief Whether the timer has reached its duration (stays true for Once mode). */
    bool is_finished() const { return m_finished; }

    /** @brief Whether the timer finished during the most recent tick. */
    bool just_finished() const { return m_times_finished_this_tick > 0; }

    /** @brief Get elapsed time since last reset. */
    std::chrono::nanoseconds elapsed() const { return m_stopwatch.elapsed(); }

    /** @brief Get elapsed time as float seconds. */
    float elapsed_secs() const { return m_stopwatch.elapsed_secs(); }

    /** @brief Get elapsed time as double seconds. */
    double elapsed_secs_f64() const { return m_stopwatch.elapsed_secs_f64(); }

    /** @brief Set elapsed time directly. */
    void set_elapsed(std::chrono::nanoseconds time) { m_stopwatch.set_elapsed(time); }

    /** @brief Get the timer's target duration. */
    std::chrono::nanoseconds duration() const { return m_duration; }

    /** @brief Set the timer's target duration. */
    void set_duration(std::chrono::nanoseconds duration) { m_duration = duration; }

    /** @brief Advance the timer to exactly finished by ticking the remaining time. */
    void finish() {
        auto rem = remaining();
        tick(rem);
    }

    /** @brief Advance the timer to 1 nanosecond before finished. */
    void almost_finish() {
        auto rem = remaining() - std::chrono::nanoseconds(1);
        tick(rem);
    }

    /** @brief Get the current timer mode. */
    TimerMode mode() const { return m_mode; }

    /** @brief Change the timer mode. Switching to Repeating while finished resets the stopwatch. */
    void set_mode(TimerMode mode) {
        if (m_mode != TimerMode::Repeating && mode == TimerMode::Repeating && m_finished) {
            m_stopwatch.reset();
            m_finished = just_finished();
        }
        m_mode = mode;
    }

    /** @brief Advance the timer by delta. Updates finished state and tracks
     *  how many times the timer completed during this tick (for Repeating mode). */
    Timer& tick(std::chrono::nanoseconds delta) {
        if (is_paused()) {
            m_times_finished_this_tick = 0;
            if (m_mode == TimerMode::Repeating) {
                m_finished = false;
            }
            return *this;
        }

        if (m_mode != TimerMode::Repeating && is_finished()) {
            m_times_finished_this_tick = 0;
            return *this;
        }

        m_stopwatch.tick(delta);
        m_finished = elapsed() >= m_duration;

        if (is_finished()) {
            if (m_mode == TimerMode::Repeating) {
                auto elapsed_ns  = elapsed().count();
                auto duration_ns = m_duration.count();
                if (duration_ns > 0) {
                    auto count                 = elapsed_ns / duration_ns;
                    m_times_finished_this_tick = static_cast<std::uint32_t>(
                        std::min(count, static_cast<decltype(count)>(std::numeric_limits<std::uint32_t>::max())));
                    set_elapsed(std::chrono::nanoseconds(elapsed_ns % duration_ns));
                } else {
                    m_times_finished_this_tick = std::numeric_limits<std::uint32_t>::max();
                    set_elapsed(std::chrono::nanoseconds(0));
                }
            } else {
                m_times_finished_this_tick = 1;
                set_elapsed(m_duration);
            }
        } else {
            m_times_finished_this_tick = 0;
        }

        return *this;
    }

    /** @brief Pause the timer. Ticks while paused do not advance elapsed. */
    void pause() { m_stopwatch.pause(); }

    /** @brief Unpause the timer. */
    void unpause() { m_stopwatch.unpause(); }

    /** @brief Check whether the timer is paused. */
    bool is_paused() const { return m_stopwatch.is_paused(); }

    /** @brief Reset to initial state: elapsed=0, not finished. */
    void reset() {
        m_stopwatch.reset();
        m_finished                 = false;
        m_times_finished_this_tick = 0;
    }

    /** @brief Fraction of duration elapsed, in [0, 1]. Returns 1 if duration is zero. */
    float fraction() const {
        if (m_duration == std::chrono::nanoseconds(0)) {
            return 1.0f;
        }
        return std::chrono::duration<float>(elapsed()).count() / std::chrono::duration<float>(m_duration).count();
    }

    /** @brief Fraction of duration remaining (1 - fraction()). */
    float fraction_remaining() const { return 1.0f - fraction(); }

    /** @brief Time remaining until finished, as float seconds. */
    float remaining_secs() const { return std::chrono::duration<float>(remaining()).count(); }

    /** @brief Time remaining until finished. */
    std::chrono::nanoseconds remaining() const { return m_duration - elapsed(); }

    /** @brief Number of times the timer completed during the most recent tick.
     *  For Repeating timers with a very small duration, this can be > 1. */
    std::uint32_t times_finished_this_tick() const { return m_times_finished_this_tick; }

   private:
    Stopwatch m_stopwatch{};
    std::chrono::nanoseconds m_duration{0};
    TimerMode m_mode                         = TimerMode::Once;
    bool m_finished                          = false;
    std::uint32_t m_times_finished_this_tick = 0;
};

}  // namespace epix::time
