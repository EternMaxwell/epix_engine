export module epix.time:virt;

import std;

import :time_clock;
import :real;

namespace epix::time {

export struct Virtual {
    std::chrono::nanoseconds max_delta = std::chrono::milliseconds(250);
    bool paused                        = false;
    double relative_speed              = 1.0;
    double effective_speed             = 1.0;
};

export template <>
struct Time<Virtual> : private Time<> {
    static constexpr std::chrono::nanoseconds DEFAULT_MAX_DELTA = std::chrono::milliseconds(250);

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

    static Time from_max_delta(std::chrono::nanoseconds max_delta) {
        Time ret;
        ret.set_max_delta(max_delta);
        return ret;
    }

    std::chrono::nanoseconds max_delta() const { return m_context.max_delta; }

    void set_max_delta(std::chrono::nanoseconds max_delta) {
        if (max_delta.count() == 0) std::abort();
        m_context.max_delta = max_delta;
    }

    float relative_speed() const { return static_cast<float>(relative_speed_f64()); }

    double relative_speed_f64() const { return m_context.relative_speed; }

    float effective_speed() const { return static_cast<float>(m_context.effective_speed); }

    double effective_speed_f64() const { return m_context.effective_speed; }

    void set_relative_speed(float ratio) { set_relative_speed_f64(static_cast<double>(ratio)); }

    void set_relative_speed_f64(double ratio) {
        if (!std::isfinite(ratio)) std::abort();
        if (ratio < 0.0) std::abort();
        m_context.relative_speed = ratio;
    }

    void toggle() { m_context.paused = !m_context.paused; }

    void pause() { m_context.paused = true; }

    void unpause() { m_context.paused = false; }

    bool is_paused() const { return m_context.paused; }

    bool was_paused() const { return m_context.effective_speed == 0.0; }

    void advance_with_raw_delta(std::chrono::nanoseconds raw_delta) {
        auto max_d         = m_context.max_delta;
        auto clamped_delta = (raw_delta > max_d) ? max_d : raw_delta;
        double eff_speed   = m_context.paused ? 0.0 : m_context.relative_speed;
        std::chrono::nanoseconds dt;
        if (eff_speed != 1.0) {
            dt = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(std::chrono::duration<double>(clamped_delta).count() * eff_speed));
        } else {
            dt = clamped_delta;
        }
        m_context.effective_speed = eff_speed;
        advance_by(dt);
    }

    const Virtual& context() const { return m_context; }
    Virtual& context_mut() { return m_context; }

    Time<> as_generic() const { return static_cast<const Time<>&>(*this); }

   private:
    Virtual m_context{};
};

export inline void update_virtual_time(Time<>& current, Time<Virtual>& virt, const Time<Real>& real) {
    auto raw_delta = real.delta();
    virt.advance_with_raw_delta(raw_delta);
    current = virt.as_generic();
}

}  // namespace time
