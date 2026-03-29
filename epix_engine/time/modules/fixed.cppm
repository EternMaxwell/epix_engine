export module epix.time:fixed;

import std;

import :time_clock;
import :virt;

namespace time {

export struct Fixed {
    std::chrono::nanoseconds timestep = std::chrono::microseconds(15625);  // 64 Hz
    std::chrono::nanoseconds overstep{0};
};

export template <>
struct Time<Fixed> : private Time<> {
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

    static Time from_duration(std::chrono::nanoseconds timestep) {
        Time ret;
        ret.set_timestep(timestep);
        return ret;
    }

    static Time from_seconds(double seconds) {
        Time ret;
        ret.set_timestep_seconds(seconds);
        return ret;
    }

    static Time from_hz(double hz) {
        Time ret;
        ret.set_timestep_hz(hz);
        return ret;
    }

    std::chrono::nanoseconds timestep() const { return m_context.timestep; }

    void set_timestep(std::chrono::nanoseconds timestep) {
        if (timestep.count() == 0) std::abort();
        m_context.timestep = timestep;
    }

    void set_timestep_seconds(double seconds) {
        if (seconds <= 0.0) std::abort();
        if (!std::isfinite(seconds)) std::abort();
        set_timestep(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds)));
    }

    void set_timestep_hz(double hz) {
        if (hz <= 0.0) std::abort();
        if (!std::isfinite(hz)) std::abort();
        set_timestep_seconds(1.0 / hz);
    }

    std::chrono::nanoseconds overstep() const { return m_context.overstep; }

    void accumulate_overstep(std::chrono::nanoseconds delta) { m_context.overstep += delta; }

    void discard_overstep(std::chrono::nanoseconds discard) {
        if (m_context.overstep > discard) {
            m_context.overstep -= discard;
        } else {
            m_context.overstep = std::chrono::nanoseconds(0);
        }
    }

    float overstep_fraction() const {
        return std::chrono::duration<float>(m_context.overstep).count() /
               std::chrono::duration<float>(m_context.timestep).count();
    }

    double overstep_fraction_f64() const {
        return std::chrono::duration<double>(m_context.overstep).count() /
               std::chrono::duration<double>(m_context.timestep).count();
    }

    bool expend() {
        auto ts = timestep();
        if (m_context.overstep >= ts) {
            m_context.overstep -= ts;
            advance_by(ts);
            return true;
        }
        return false;
    }

    const Fixed& context() const { return m_context; }
    Fixed& context_mut() { return m_context; }

    Time<> as_generic() const { return static_cast<const Time<>&>(*this); }

   private:
    Fixed m_context{};
};

}  // namespace time
