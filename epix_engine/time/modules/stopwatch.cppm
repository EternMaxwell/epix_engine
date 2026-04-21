module;
#ifndef EPIX_IMPORT_STD
#include <chrono>
#endif

export module epix.time:stopwatch;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::time {

/** @brief A pausable stopwatch that accumulates elapsed time from explicit ticks. */
export struct Stopwatch {
    Stopwatch() = default;

    /** @brief Get total elapsed time (only accumulated while unpaused). */
    std::chrono::nanoseconds elapsed() const { return m_elapsed; }

    /** @brief Get elapsed time as float seconds. */
    float elapsed_secs() const { return std::chrono::duration<float>(m_elapsed).count(); }

    /** @brief Get elapsed time as double seconds. */
    double elapsed_secs_f64() const { return std::chrono::duration<double>(m_elapsed).count(); }

    /** @brief Set elapsed time directly. */
    void set_elapsed(std::chrono::nanoseconds time) { m_elapsed = time; }

    /** @brief Advance the stopwatch by delta. Does nothing if paused. */
    Stopwatch& tick(std::chrono::nanoseconds delta) {
        if (!m_paused) {
            m_elapsed += delta;
        }
        return *this;
    }

    /** @brief Pause the stopwatch. Subsequent ticks will not accumulate. */
    void pause() { m_paused = true; }

    /** @brief Unpause the stopwatch. */
    void unpause() { m_paused = false; }

    /** @brief Check whether the stopwatch is paused. */
    bool is_paused() const { return m_paused; }

    /** @brief Reset elapsed time to zero. */
    void reset() { m_elapsed = std::chrono::nanoseconds(0); }

   private:
    std::chrono::nanoseconds m_elapsed{0};
    bool m_paused = false;
};

}  // namespace epix::time
