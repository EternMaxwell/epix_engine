#include "epix/app/profiler.h"

using namespace epix::app;
EPIX_API void ScheduleProfiler::reset() {
    m_count = 0;

    m_build_time_last = 0.0;
    m_build_time_avg  = 0.0;
    m_run_time_last   = 0.0;
    m_run_time_avg    = 0.0;

    m_empty_zone = 1.0;
    m_factor     = 0.1;
}
EPIX_API double ScheduleProfiler::build_time_last() const {
    return m_build_time_last;
}
EPIX_API double ScheduleProfiler::build_time_avg() const {
    return m_count ? m_build_time_avg / (1 - m_empty_zone) : m_build_time_avg;
}
EPIX_API double ScheduleProfiler::run_time_last() const {
    return m_run_time_last;
}
EPIX_API double ScheduleProfiler::run_time_avg() const {
    return m_count ? m_run_time_avg / (1 - m_empty_zone) : m_run_time_avg;
}
EPIX_API void ScheduleProfiler::push_time(double build_time, double run_time) {
    m_count++;

    m_build_time_last = build_time;
    m_build_time_avg =
        m_build_time_avg * (1.0 - m_factor) + build_time * m_factor;
    m_run_time_last = run_time;
    m_run_time_avg  = m_run_time_avg * (1.0 - m_factor) + run_time * m_factor;

    m_empty_zone *= (1.0 - m_factor);
}
EPIX_API void ScheduleProfiler::push_set_count(size_t count) {
    m_set_count = count;
}
EPIX_API size_t ScheduleProfiler::set_count() const { return m_set_count; }
EPIX_API void ScheduleProfiler::push_system_count(size_t count) {
    m_system_count = count;
}
EPIX_API size_t ScheduleProfiler::system_count() const {
    return m_system_count;
}
EPIX_API void ScheduleProfiler::set_factor(double factor) {
    m_factor = std::clamp(factor, 0.0, 1.0);
}

EPIX_API void AppProfiler::reset() {
    m_count      = 0;
    m_time_last  = 0.0;
    m_time_avg   = 0.0;
    m_empty_zone = 1.0;
    m_factor     = 0.1;
    std::unique_lock lock(m_mutex);
    for (auto&& [label, profiler] : m_schedule_profilers) {
        profiler.reset();
    }
}
EPIX_API void AppProfiler::set_factor(double factor) {
    m_factor = std::clamp(factor, 0.0, 1.0);
}
EPIX_API double AppProfiler::time_last() const { return m_time_last; }
EPIX_API double AppProfiler::time_avg() const {
    return m_count ? m_time_avg / (1 - m_empty_zone) : m_time_avg;
}
EPIX_API void AppProfiler::push_time(double time) {
    m_count++;
    m_time_last = time;
    m_time_avg  = m_time_avg * (1.0 - m_factor) + time * m_factor;
    m_empty_zone *= (1.0 - m_factor);
}
EPIX_API entt::dense_map<ScheduleLabel, std::shared_ptr<ScheduleProfiler>>
AppProfiler::schedule_profilers() const {
    std::unique_lock lock(m_mutex);
    return m_schedule_profilers;
}
EPIX_API ScheduleProfiler& AppProfiler::schedule_profiler(
    const ScheduleLabel& label
) {
    std::unique_lock lock(m_mutex);
    if (auto it = m_schedule_profilers.find(label);
        it != m_schedule_profilers.end()) {
        return *it->second;
    } else {
        auto profiler = std::make_shared<ScheduleProfiler>();
        return *m_schedule_profilers.emplace(label, profiler).first->second;
    }
}
EPIX_API ScheduleProfiler* AppProfiler::get_schedule_profiler(
    const ScheduleLabel& label
) {
    std::unique_lock lock(m_mutex);
    auto it = m_schedule_profilers.find(label);
    if (it != m_schedule_profilers.end()) {
        return it->second.get();
    } else {
        return nullptr;
    }
}
EPIX_API const ScheduleProfiler* AppProfiler::get_schedule_profiler(
    const ScheduleLabel& label
) const {
    std::unique_lock lock(m_mutex);
    auto it = m_schedule_profilers.find(label);
    if (it != m_schedule_profilers.end()) {
        return it->second.get();
    } else {
        return nullptr;
    }
}