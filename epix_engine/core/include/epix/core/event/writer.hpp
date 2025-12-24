#pragma once

#include "../system/from_param.hpp"
#include "../system/param.hpp"
#include "events.hpp"

namespace epix::core::event {
template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>> m_events;
    EventWriter(ResMut<Events<T>> events) : m_events(events) {}

   public:
    static EventWriter<T> from_param(ResMut<Events<T>> events) { return EventWriter<T>(events); }

    void write(const T& event) { m_events->push(event); }
    void write(T&& event) { m_events->push(std::move(event)); }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
    }
};
static_assert(system::is_from_param<EventWriter<int>>);
static_assert(system::valid_system_param<system::SystemParam<EventWriter<int>>>);
};  // namespace epix::core::event