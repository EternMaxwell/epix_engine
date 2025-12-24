#pragma once

#include <ranges>

#include "../system/from_param.hpp"
#include "events.hpp"

namespace epix::core::event {
template <typename T>
struct EventCursor {
    uint32_t index = 0;
};
template <typename T>
struct EventReader {
   private:
    system::Local<EventCursor<T>> _cursor;
    Res<Events<T>> _events;
    EventReader(system::Local<EventCursor<T>> cursor, Res<Events<T>> events) : _cursor(cursor), _events(events) {}

   public:
    static EventReader<T> from_param(system::Local<EventCursor<T>> cursor, Res<Events<T>> events) {
        cursor->index = std::max(cursor->index, events->head());
        cursor->index = std::min(cursor->index, events->tail());
        return EventReader<T>(cursor, events);
    }

    auto read() {
        return std::views::iota(_cursor->index, _events->tail()) |
               std::views::transform([this](uint32_t index) mutable {
                   _cursor->index++;
                   return *_events->get(index);
               });
    }
    auto read_with_id() {
        return std::views::iota(_cursor->index, _events->tail()) |
               std::views::transform([this](uint32_t index) mutable {
                   auto event = _events->get(index);
                   _cursor->index++;
                   return std::tuple<uint32_t, const T&>(index, *event);
               });
    }
    uint32_t size() const { return _events->tail() - _cursor->index; }
    bool empty() const { return _cursor->index == _events->tail(); }
    void clear() { _cursor->index = _events->tail(); }
    std::optional<std::reference_wrapper<const T>> read_one() {
        auto event = _events->get(_cursor->index);
        if (event) {
            _cursor->index++;
            return std::ref(*event);
        } else {
            return std::nullopt;
        }
    }
    std::optional<std::tuple<const T&, uint32_t>> read_one_index() {
        auto event = _events->get(_cursor->index);
        if (event) {
            uint32_t current_index = _cursor->index;
            _cursor->index++;
            return std::tuple<const T&, uint32_t>(*event, current_index);
        } else {
            return std::nullopt;
        }
    }
};

static_assert(system::is_from_param<EventReader<int>>);
static_assert(system::valid_system_param<system::SystemParam<EventReader<int>>>);
}  // namespace epix::core::event