export module epix.time:time_clock;

import std;

namespace epix::time {

inline std::chrono::nanoseconds duration_rem(std::chrono::nanoseconds dividend, std::chrono::nanoseconds divisor) {
    return std::chrono::nanoseconds(dividend.count() % divisor.count());
}

/** @brief Default context tag for the generic `Time<>` clock. */
export struct GenericTag {};

/** @brief Core clock type tracking delta time, total elapsed time, and wrapped elapsed time.
 *  @tparam T Context tag type. Specialize with `Real`, `Virtual`, `Fixed`, or use the
 *           default `GenericTag` for a context-free clock (`Time<>`). */
export template <typename T = GenericTag>
struct Time {
    /** @brief Default wrap period (1 hour). Elapsed time wraps around this period. */
    static constexpr std::chrono::nanoseconds DEFAULT_WRAP_PERIOD = std::chrono::hours(1);

    Time() = default;

    /** @brief Construct with an explicit context value. */
    explicit Time(T ctx) : m_context(std::move(ctx)), m_wrap_period(DEFAULT_WRAP_PERIOD) {}

    /** @brief Advance the clock by the given delta, updating all cached fields. */
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

    /** @brief Advance the clock to the given absolute elapsed time. Must be >= current elapsed. */
    void advance_to(std::chrono::nanoseconds elapsed) {
        m_assert(elapsed >= m_elapsed);
        advance_by(elapsed - m_elapsed);
    }

    /** @brief Get the wrap period for elapsed_wrapped calculations. */
    std::chrono::nanoseconds wrap_period() const { return m_wrap_period; }
    /** @brief Set the wrap period. Must be non-zero. */
    void set_wrap_period(std::chrono::nanoseconds wrap_period) {
        m_assert(wrap_period.count() != 0);
        m_wrap_period = wrap_period;
    }

    /** @brief Duration of the last advance (delta time). */
    std::chrono::nanoseconds delta() const { return m_delta; }
    /** @brief Delta time as float seconds. */
    float delta_secs() const { return m_delta_secs; }
    /** @brief Delta time as double seconds. */
    double delta_secs_f64() const { return m_delta_secs_f64; }

    /** @brief Total elapsed time since start. */
    std::chrono::nanoseconds elapsed() const { return m_elapsed; }
    /** @brief Total elapsed time as float seconds. */
    float elapsed_secs() const { return m_elapsed_secs; }
    /** @brief Total elapsed time as double seconds. */
    double elapsed_secs_f64() const { return m_elapsed_secs_f64; }

    /** @brief Elapsed time wrapped by wrap_period. */
    std::chrono::nanoseconds elapsed_wrapped() const { return m_elapsed_wrapped; }
    /** @brief Wrapped elapsed time as float seconds. */
    float elapsed_secs_wrapped() const { return m_elapsed_secs_wrapped; }
    /** @brief Wrapped elapsed time as double seconds. */
    double elapsed_secs_wrapped_f64() const { return m_elapsed_secs_wrapped_f64; }

    /** @brief Get const reference to the context value. */
    const T& context() const { return m_context; }
    /** @brief Get mutable reference to the context value. */
    T& context_mut() { return m_context; }

    /** @brief Convert to a `Time<GenericTag>` by copying all timing fields. */
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

}  // namespace epix::time
