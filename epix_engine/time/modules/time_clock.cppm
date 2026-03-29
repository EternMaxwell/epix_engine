export module epix.time:time_clock;

import std;

namespace time {

inline std::chrono::nanoseconds duration_rem(std::chrono::nanoseconds dividend, std::chrono::nanoseconds divisor) {
    return std::chrono::nanoseconds(dividend.count() % divisor.count());
}

export struct GenericTag {};

export template <typename T = GenericTag>
struct Time {
    static constexpr std::chrono::nanoseconds DEFAULT_WRAP_PERIOD = std::chrono::hours(1);

    Time() = default;

    explicit Time(T ctx) : m_context(std::move(ctx)), m_wrap_period(DEFAULT_WRAP_PERIOD) {}

    void advance_by(std::chrono::nanoseconds delta) {
        m_delta          = delta;
        m_delta_secs     = std::chrono::duration<float>(delta).count();
        m_delta_secs_f64 = std::chrono::duration<double>(delta).count();
        m_elapsed += delta;
        m_elapsed_secs             = std::chrono::duration<float>(m_elapsed).count();
        m_elapsed_secs_f64         = std::chrono::duration<double>(m_elapsed).count();
        m_elapsed_wrapped          = duration_rem(m_elapsed, m_wrap_period);
        m_elapsed_secs_wrapped     = std::chrono::duration<float>(m_elapsed_wrapped).count();
        m_elapsed_secs_wrapped_f64 = std::chrono::duration<double>(m_elapsed_wrapped).count();
    }

    void advance_to(std::chrono::nanoseconds elapsed) {
        m_assert(elapsed >= m_elapsed);
        advance_by(elapsed - m_elapsed);
    }

    std::chrono::nanoseconds wrap_period() const { return m_wrap_period; }
    void set_wrap_period(std::chrono::nanoseconds wrap_period) {
        m_assert(wrap_period.count() != 0);
        m_wrap_period = wrap_period;
    }

    std::chrono::nanoseconds delta() const { return m_delta; }
    float delta_secs() const { return m_delta_secs; }
    double delta_secs_f64() const { return m_delta_secs_f64; }

    std::chrono::nanoseconds elapsed() const { return m_elapsed; }
    float elapsed_secs() const { return m_elapsed_secs; }
    double elapsed_secs_f64() const { return m_elapsed_secs_f64; }

    std::chrono::nanoseconds elapsed_wrapped() const { return m_elapsed_wrapped; }
    float elapsed_secs_wrapped() const { return m_elapsed_secs_wrapped; }
    double elapsed_secs_wrapped_f64() const { return m_elapsed_secs_wrapped_f64; }

    const T& context() const { return m_context; }
    T& context_mut() { return m_context; }

    Time<GenericTag> as_generic() const {
        Time<GenericTag> g;
        g.m_wrap_period              = m_wrap_period;
        g.m_delta                    = m_delta;
        g.m_delta_secs               = m_delta_secs;
        g.m_delta_secs_f64           = m_delta_secs_f64;
        g.m_elapsed                  = m_elapsed;
        g.m_elapsed_secs             = m_elapsed_secs;
        g.m_elapsed_secs_f64         = m_elapsed_secs_f64;
        g.m_elapsed_wrapped          = m_elapsed_wrapped;
        g.m_elapsed_secs_wrapped     = m_elapsed_secs_wrapped;
        g.m_elapsed_secs_wrapped_f64 = m_elapsed_secs_wrapped_f64;
        return g;
    }

    template <typename U>
    friend struct Time;

   private:
    static void m_assert(bool cond) {
        if (!cond) std::abort();
    }

    T m_context{};
    std::chrono::nanoseconds m_wrap_period = DEFAULT_WRAP_PERIOD;
    std::chrono::nanoseconds m_delta{0};
    float m_delta_secs      = 0.0f;
    double m_delta_secs_f64 = 0.0;
    std::chrono::nanoseconds m_elapsed{0};
    float m_elapsed_secs      = 0.0f;
    double m_elapsed_secs_f64 = 0.0;
    std::chrono::nanoseconds m_elapsed_wrapped{0};
    float m_elapsed_secs_wrapped      = 0.0f;
    double m_elapsed_secs_wrapped_f64 = 0.0;
};

}  // namespace time
