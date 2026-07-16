#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
#endif

#include <epix/ecs/event/events.hpp>
#include <epix/ecs/system/local.hpp>
#include <epix/ecs/system.hpp>

namespace epix::ecs {

EPIX_EXPORT template <typename T>
struct EventReader {
   private:
    Local<EventCursor<T>> _cursor;
    Res<Events<T>> _events;
    EventReader(Local<EventCursor<T>> cursor, Res<Events<T>> events) noexcept : _cursor(cursor), _events(events) {}

   public:
    static EventReader from_param(Local<EventCursor<T>> cursor, Res<Events<T>> events) {
        cursor->index = std::clamp(cursor->index, events->head(), events->tail());
        return EventReader(cursor, events);
    }
    auto read() {
        return std::views::transform(std::views::iota(_cursor->index, _events->tail()), [this](std::uint32_t index) {
            _cursor->index++;
            return *_events->get(index);
        });
    }
    auto read_with_id() {
        return std::views::transform(std::views::iota(_cursor->index, _events->tail()), [this](std::uint32_t index) {
            auto event = _events->get(index);
            _cursor->index++;
            return std::tuple<std::uint32_t, const T&>(index, *event);
        });
    }
    std::uint32_t size() const noexcept { return _events->tail() - _cursor->index; }
    bool empty() const noexcept { return _cursor->index == _events->tail(); }
    std::uint32_t position() const noexcept { return _cursor->index; }
    void clear() noexcept { _cursor->index = _events->tail(); }
    std::optional<std::reference_wrapper<const T>> read_one() {
        if (auto event = _events->get(_cursor->index)) {
            _cursor->index++;
            return std::ref(*event);
        }
        return std::nullopt;
    }
    std::optional<std::tuple<const T&, std::uint32_t>> read_one_index() {
        if (auto event = _events->get(_cursor->index)) {
            return std::tuple<const T&, std::uint32_t>(*event, _cursor->index++);
        }
        return std::nullopt;
    }
};

EPIX_EXPORT template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>> m_events;
    EventWriter(ResMut<Events<T>> events) noexcept : m_events(events) {}

   public:
    static EventWriter from_param(ResMut<Events<T>> events) { return EventWriter(events); }
    std::uint32_t position() const noexcept { return m_events->tail(); }
    void advance_head(std::uint32_t new_head) { m_events->advance_head(new_head); }
    void write(const T& event) { m_events->push(event); }
    void write(T&& event) { m_events->push(std::move(event)); }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
    }
};

static_assert(internal::from_param<EventReader<int>>);
static_assert(system_param<EventReader<int>>);
static_assert(internal::from_param<EventWriter<int>>);
static_assert(system_param<EventWriter<int>>);

}  // namespace epix::ecs
