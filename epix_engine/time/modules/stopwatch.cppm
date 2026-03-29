export module epix.time:stopwatch;

import std;

namespace time {

export struct Stopwatch {
    Stopwatch() = default;

    std::chrono::nanoseconds elapsed() const { return m_elapsed; }

    float elapsed_secs() const { return std::chrono::duration<float>(m_elapsed).count(); }

    double elapsed_secs_f64() const { return std::chrono::duration<double>(m_elapsed).count(); }

    void set_elapsed(std::chrono::nanoseconds time) { m_elapsed = time; }

    Stopwatch& tick(std::chrono::nanoseconds delta) {
        if (!m_paused) {
            m_elapsed += delta;
        }
        return *this;
    }

    void pause() { m_paused = true; }

    void unpause() { m_paused = false; }

    bool is_paused() const { return m_paused; }

    void reset() { m_elapsed = std::chrono::nanoseconds(0); }

   private:
    std::chrono::nanoseconds m_elapsed{0};
    bool m_paused = false;
};

}  // namespace time
