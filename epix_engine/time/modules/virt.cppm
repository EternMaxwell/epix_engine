export module epix.time:virt;

import std;

import :time_clock;
import :real;

namespace epix::time {

/** @brief Context for `Time<Virtual>`. Stores speed, pause state, and max delta clamp. */
export struct Virtual {
    /** @brief Maximum delta to accept per update (clamped). Prevents spiral-of-death. */
    std::chrono::nanoseconds max_delta = std::chrono::milliseconds(250);
    /** @brief Whether virtual time is paused (effective speed becomes 0). */
    bool paused = false;
    /** @brief User-set speed multiplier (>= 0). 1.0 = real-time. */
    double relative_speed = 1.0;
    /** @brief Computed effective speed after accounting for pause state. */
    double effective_speed = 1.0;
};

/** @brief Virtual (game) time. Derived from real time, but can be paused,
 *  sped up, or slowed down. Delta is clamped to max_delta before scaling. */
export template <>
struct Time<Virtual> : private Time<> {
    /** @brief Default maximum delta clamp (250 ms). */
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

    /** @brief Create a Virtual time with a custom max delta clamp. */
    static Time from_max_delta(std::chrono::nanoseconds max_delta) {
        Time ret;
        ret.set_max_delta(max_delta);
        return ret;
    }

    /** @brief Get the maximum delta clamp. */
    std::chrono::nanoseconds max_delta() const { return m_context.max_delta; }

    /** @brief Set the maximum delta clamp. Must be non-zero. */
    void set_max_delta(std::chrono::nanoseconds max_delta) {
        if (max_delta.count() == 0) std::abort();
        m_context.max_delta = max_delta;
    }

    /** @brief Get the user-set relative speed as float. */
    float relative_speed() const { return static_cast<float>(relative_speed_f64()); }

    /** @brief Get the user-set relative speed as double. */
    double relative_speed_f64() const { return m_context.relative_speed; }

    /** @brief Get the effective speed (accounting for pause) as float. */
    float effective_speed() const { return static_cast<float>(m_context.effective_speed); }

    /** @brief Get the effective speed (accounting for pause) as double. */
    double effective_speed_f64() const { return m_context.effective_speed; }

    /** @brief Set the relative speed multiplier (float). Must be finite and >= 0. */
    void set_relative_speed(float ratio) { set_relative_speed_f64(static_cast<double>(ratio)); }

    /** @brief Set the relative speed multiplier (double). Must be finite and >= 0. */
    void set_relative_speed_f64(double ratio) {
        if (!std::isfinite(ratio)) std::abort();
        if (ratio < 0.0) std::abort();
        m_context.relative_speed = ratio;
    }

    /** @brief Toggle between paused and unpaused. */
    void toggle() { m_context.paused = !m_context.paused; }

    /** @brief Pause virtual time (effective speed becomes 0). */
    void pause() { m_context.paused = true; }

    /** @brief Unpause virtual time. */
    void unpause() { m_context.paused = false; }

    /** @brief Check whether virtual time is currently paused. */
    bool is_paused() const { return m_context.paused; }

    /** @brief Check whether virtual time was effectively paused last tick (speed was 0). */
    bool was_paused() const { return m_context.effective_speed == 0.0; }

    /** @brief Advance virtual time from a raw (real) delta. The delta is clamped to
     *  max_delta, then multiplied by relative_speed (or 0 if paused). */
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

    /** @brief Get const reference to the Virtual context. */
    const Virtual& context() const { return m_context; }
    /** @brief Get mutable reference to the Virtual context. */
    Virtual& context_mut() { return m_context; }

    /** @brief Convert to a generic `Time<>` by copying all timing fields. */
    Time<> as_generic() const { return static_cast<const Time<>&>(*this); }

   private:
    Virtual m_context{};
};

/** @brief Update virtual time from real time. Feeds real delta into virtual advance,
 *  then copies virtual time into the generic `Time<>` current clock. */
export inline void update_virtual_time(Time<>& current, Time<Virtual>& virt, const Time<Real>& real) {
    auto raw_delta = real.delta();
    virt.advance_with_raw_delta(raw_delta);
    current = virt.as_generic();
}

}  // namespace epix::time
