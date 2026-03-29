export module epix.time:timer;

import std;

import :stopwatch;

namespace epix::time {

export enum class TimerMode {
    Once,
    Repeating,
};

export struct Timer {
    Timer() = default;

    Timer(std::chrono::nanoseconds duration, TimerMode mode) : m_duration(duration), m_mode(mode) {}

    static Timer from_seconds(float duration, TimerMode mode) {
        return Timer(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(duration)),
                     mode);
    }

    bool is_finished() const { return m_finished; }

    bool just_finished() const { return m_times_finished_this_tick > 0; }

    std::chrono::nanoseconds elapsed() const { return m_stopwatch.elapsed(); }

    float elapsed_secs() const { return m_stopwatch.elapsed_secs(); }

    double elapsed_secs_f64() const { return m_stopwatch.elapsed_secs_f64(); }

    void set_elapsed(std::chrono::nanoseconds time) { m_stopwatch.set_elapsed(time); }

    std::chrono::nanoseconds duration() const { return m_duration; }

    void set_duration(std::chrono::nanoseconds duration) { m_duration = duration; }

    void finish() {
        auto rem = remaining();
        tick(rem);
    }

    void almost_finish() {
        auto rem = remaining() - std::chrono::nanoseconds(1);
        tick(rem);
    }

    TimerMode mode() const { return m_mode; }

    void set_mode(TimerMode mode) {
        if (m_mode != TimerMode::Repeating && mode == TimerMode::Repeating && m_finished) {
            m_stopwatch.reset();
            m_finished = just_finished();
        }
        m_mode = mode;
    }

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

    void pause() { m_stopwatch.pause(); }

    void unpause() { m_stopwatch.unpause(); }

    bool is_paused() const { return m_stopwatch.is_paused(); }

    void reset() {
        m_stopwatch.reset();
        m_finished                 = false;
        m_times_finished_this_tick = 0;
    }

    float fraction() const {
        if (m_duration == std::chrono::nanoseconds(0)) {
            return 1.0f;
        }
        return std::chrono::duration<float>(elapsed()).count() / std::chrono::duration<float>(m_duration).count();
    }

    float fraction_remaining() const { return 1.0f - fraction(); }

    float remaining_secs() const { return std::chrono::duration<float>(remaining()).count(); }

    std::chrono::nanoseconds remaining() const { return m_duration - elapsed(); }

    std::uint32_t times_finished_this_tick() const { return m_times_finished_this_tick; }

   private:
    Stopwatch m_stopwatch{};
    std::chrono::nanoseconds m_duration{0};
    TimerMode m_mode                         = TimerMode::Once;
    bool m_finished                          = false;
    std::uint32_t m_times_finished_this_tick = 0;
};

}  // namespace time
