module;
#ifndef EPIX_IMPORT_STD
#include <optional>
#include <chrono>
#endif

export module epix.time:real;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :time_clock;

namespace epix::time {

/** @brief Context for `Time<Real>`. Tracks wall-clock time points for startup,
 *  first update, and last update. */
export struct Real {
    /** @brief The steady_clock time point when the app started. */
    std::chrono::steady_clock::time_point startup = std::chrono::steady_clock::now();
    /** @brief Time point of the first update call, if any. */
    std::optional<std::chrono::steady_clock::time_point> first_update;
    /** @brief Time point of the most recent update call, if any. */
    std::optional<std::chrono::steady_clock::time_point> last_update;
};

/** @brief Wall-clock time. Advances based on real steady_clock measurements.
 *  Used as the root time source; virtual and fixed time derive from this. */
template <>
struct Time<Real> : private Time<> {
    // Re-export base methods
    using Time<>::wrap_period;
    using Time<>::set_wrap_period;
    using Time<>::delta;
    using Time<>::delta_secs;
    using Time<>::delta_secs_f64;
    using Time<>::elapsed;
    using Time<>::elapsed_secs;
    using Time<>::elapsed_secs_f64;
    using Time<>::elapsed_wrapped;
    using Time<>::elapsed_secs_wrapped;
    using Time<>::elapsed_secs_wrapped_f64;
    using Time<>::advance_by;
    using Time<>::advance_to;

    Time() = default;

    /** @brief Construct with a custom startup time point. */
    explicit Time(std::chrono::steady_clock::time_point startup) : m_context{.startup = startup} {}

    /** @brief Update using the current steady_clock::now(). */
    void update() { update_with_instant(std::chrono::steady_clock::now()); }

    /** @brief Update by advancing a fixed duration from the last update point. */
    void update_with_duration(std::chrono::nanoseconds duration) {
        auto last = m_context.last_update.value_or(m_context.startup);
        update_with_instant(last + duration);
    }

    /** @brief Update to a specific time point. On first call, records first_update
     *  without advancing. On subsequent calls, computes delta from last_update. */
    void update_with_instant(std::chrono::steady_clock::time_point instant) {
        if (!m_context.last_update) {
            m_context.first_update = instant;
            m_context.last_update  = instant;
            return;
        }
        auto last  = *m_context.last_update;
        auto delta = (instant > last) ? std::chrono::duration_cast<std::chrono::nanoseconds>(instant - last)
                                      : std::chrono::nanoseconds(0);
        advance_by(delta);
        m_context.last_update = instant;
    }

    /** @brief Get the startup time point. */
    std::chrono::steady_clock::time_point startup() const { return m_context.startup; }

    /** @brief Get the time point of the first update, if any. */
    std::optional<std::chrono::steady_clock::time_point> first_update() const { return m_context.first_update; }

    /** @brief Get the time point of the most recent update, if any. */
    std::optional<std::chrono::steady_clock::time_point> last_update() const { return m_context.last_update; }

    /** @brief Get const reference to the Real context. */
    const Real& context() const { return m_context; }
    /** @brief Get mutable reference to the Real context. */
    Real& context_mut() { return m_context; }

    /** @brief Convert to a generic `Time<>` by copying all timing fields. */
    Time<> as_generic() const { return static_cast<const Time<>&>(*this); }

   private:
    Real m_context{};
};

}  // namespace epix::time
