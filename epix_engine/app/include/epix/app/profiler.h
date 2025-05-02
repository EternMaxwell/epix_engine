#pragma once

#include "schedule.h"

namespace epix::app {
struct ScheduleProfiler {
   private:
    size_t m_count = 0;
    size_t m_set_count = 0;
    size_t m_system_count = 0;

    double m_run_time_last     = 0.0;
    double m_run_time_avg      = 0.0;
    double m_flush_time_last   = 0.0;
    double m_flush_time_avg    = 0.0;
    double m_build_time_last   = 0.0;
    double m_build_time_avg    = 0.0;
    double m_prepare_time_last = 0.0;
    double m_prepare_time_avg  = 0.0;

    double m_empty_zone = 1.0;
    double m_factor     = 0.1;

   public:
    EPIX_API void reset();
    EPIX_API double flush_time_last() const;
    EPIX_API double flush_time_avg() const;
    EPIX_API double build_time_last() const;
    EPIX_API double build_time_avg() const;
    EPIX_API double prepare_time_last() const;
    EPIX_API double prepare_time_avg() const;
    EPIX_API double run_time_last() const;
    EPIX_API double run_time_avg() const;
    EPIX_API size_t set_count() const;
    EPIX_API size_t system_count() const;
    EPIX_API void push_time(
        double flush_time,
        double build_time,
        double prepare_time,
        double run_time
    );
    EPIX_API void push_set_count(size_t count);
    EPIX_API void push_system_count(size_t count);
    EPIX_API void set_factor(double factor);
};

struct AppProfiler {
   private:
    size_t m_count = 0;

    double m_time_last = 0.0;
    double m_time_avg  = 0.0;

    double m_empty_zone = 1.0;
    double m_factor     = 0.1;

    entt::dense_map<ScheduleLabel, ScheduleProfiler> m_schedule_profilers;

   public:
    EPIX_API void reset();
    EPIX_API void set_factor(double factor);
    EPIX_API double time_last() const;
    EPIX_API double time_avg() const;
    EPIX_API void push_time(double time);
    EPIX_API const entt::dense_map<ScheduleLabel, ScheduleProfiler>&
    schedule_profilers() const;
    EPIX_API entt::dense_map<ScheduleLabel, ScheduleProfiler>&
    schedule_profilers();
    EPIX_API ScheduleProfiler& schedule_profiler(const ScheduleLabel& label);
    EPIX_API ScheduleProfiler* get_schedule_profiler(const ScheduleLabel& label
    );
    EPIX_API const ScheduleProfiler* get_schedule_profiler(
        const ScheduleLabel& label
    ) const;
};
}  // namespace epix::app