export module epix.time:real;

import std;

import :time_clock;

namespace epix::time {

export struct Real {
    std::chrono::steady_clock::time_point startup = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> first_update;
    std::optional<std::chrono::steady_clock::time_point> last_update;
};

export template <>
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

    explicit Time(std::chrono::steady_clock::time_point startup) : m_context{.startup = startup} {}

    void update() { update_with_instant(std::chrono::steady_clock::now()); }

    void update_with_duration(std::chrono::nanoseconds duration) {
        auto last = m_context.last_update.value_or(m_context.startup);
        update_with_instant(last + duration);
    }

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

    std::chrono::steady_clock::time_point startup() const { return m_context.startup; }

    std::optional<std::chrono::steady_clock::time_point> first_update() const { return m_context.first_update; }

    std::optional<std::chrono::steady_clock::time_point> last_update() const { return m_context.last_update; }

    const Real& context() const { return m_context; }
    Real& context_mut() { return m_context; }

    Time<> as_generic() const { return static_cast<const Time<>&>(*this); }

   private:
    Real m_context{};
};

}  // namespace time
