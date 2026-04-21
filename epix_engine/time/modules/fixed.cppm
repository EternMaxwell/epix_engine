module;
#ifndef EPIX_IMPORT_STD
#include <cmath>
#include <cstdlib>
#include <chrono>
#endif

export module epix.time:fixed;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :time_clock;
import :virt;

namespace epix::time {

/** @brief Context for `Time<Fixed>`. Stores the fixed timestep duration
 *  and the accumulated overstep from real/virtual time. */
export struct Fixed {
    /** @brief Duration of each fixed timestep (default 64 Hz, ~15.625 ms). */
    std::chrono::nanoseconds timestep = std::chrono::microseconds(15625);  // 64 Hz
    /** @brief Accumulated real time not yet consumed by a fixed step. */
    std::chrono::nanoseconds overstep{0};
};

/** @brief Fixed-timestep time. Each tick advances by exactly one timestep.
 *  Used in the FixedMain schedule loop; `expend()` is called repeatedly
 *  to consume accumulated overstep one timestep at a time. */
template <>
struct Time<Fixed> : private Time<> {
    /** @brief Default timestep (64 Hz, ~15.625 ms). */
    static constexpr std::chrono::nanoseconds DEFAULT_TIMESTEP = std::chrono::microseconds(15625);

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

    /** @brief Create with a custom timestep duration. */
    static Time from_duration(std::chrono::nanoseconds timestep) {
        Time ret;
        ret.set_timestep(timestep);
        return ret;
    }

    /** @brief Create with a timestep specified in seconds. */
    static Time from_seconds(double seconds) {
        Time ret;
        ret.set_timestep_seconds(seconds);
        return ret;
    }

    /** @brief Create with a timestep specified as a frequency in Hz. */
    static Time from_hz(double hz) {
        Time ret;
        ret.set_timestep_hz(hz);
        return ret;
    }

    /** @brief Get the fixed timestep duration. */
    std::chrono::nanoseconds timestep() const { return m_context.timestep; }

    /** @brief Set the fixed timestep duration. Must be non-zero. */
    void set_timestep(std::chrono::nanoseconds timestep) {
        if (timestep.count() == 0) std::abort();
        m_context.timestep = timestep;
    }

    /** @brief Set the timestep from a duration in seconds. Must be positive and finite. */
    void set_timestep_seconds(double seconds) {
        if (seconds <= 0.0) std::abort();
        if (!std::isfinite(seconds)) std::abort();
        set_timestep(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds)));
    }

    /** @brief Set the timestep from a frequency in Hz. Must be positive and finite. */
    void set_timestep_hz(double hz) {
        if (hz <= 0.0) std::abort();
        if (!std::isfinite(hz)) std::abort();
        set_timestep_seconds(1.0 / hz);
    }

    /** @brief Get the accumulated overstep (unconsumed real time). */
    std::chrono::nanoseconds overstep() const { return m_context.overstep; }

    /** @brief Add delta time to the overstep accumulator. Called once per frame
     *  from virtual time delta before the fixed loop begins. */
    void accumulate_overstep(std::chrono::nanoseconds delta) { m_context.overstep += delta; }

    /** @brief Subtract from accumulated overstep, clamping to zero. */
    void discard_overstep(std::chrono::nanoseconds discard) {
        if (m_context.overstep > discard) {
            m_context.overstep -= discard;
        } else {
            m_context.overstep = std::chrono::nanoseconds(0);
        }
    }

    /** @brief Fraction of one timestep that has been accumulated (overstep / timestep) as float. */
    float overstep_fraction() const {
        return std::chrono::duration<float>(m_context.overstep).count() /
               std::chrono::duration<float>(m_context.timestep).count();
    }

    /** @brief Fraction of one timestep that has been accumulated (overstep / timestep) as double. */
    double overstep_fraction_f64() const {
        return std::chrono::duration<double>(m_context.overstep).count() /
               std::chrono::duration<double>(m_context.timestep).count();
    }

    /** @brief Try to consume one timestep from overstep. If enough time has
     *  accumulated, subtracts one timestep, advances the clock, and returns true.
     *  Returns false if not enough overstep remains. */
    bool expend() {
        auto ts = timestep();
        if (m_context.overstep >= ts) {
            m_context.overstep -= ts;
            advance_by(ts);
            return true;
        }
        return false;
    }

    /** @brief Get const reference to the Fixed context. */
    const Fixed& context() const { return m_context; }
    /** @brief Get mutable reference to the Fixed context. */
    Fixed& context_mut() { return m_context; }

    /** @brief Convert to a generic `Time<>` by copying all timing fields. */
    Time<> as_generic() const { return static_cast<const Time<>&>(*this); }

   private:
    Fixed m_context{};
};

}  // namespace epix::time
