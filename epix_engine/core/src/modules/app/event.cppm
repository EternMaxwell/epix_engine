module;

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <ranges>
#include <tuple>

export module epix.core:app.event;

import :system;
import :ticks;
import :world;

namespace core {
template <std::movable T>
struct Events {
   private:
    std::deque<T> m_events;  // event lifetime pair
    std::deque<std::uint32_t> m_lifetimes;
    std::uint32_t m_head;
    std::uint32_t m_tail;  // m_tail - m_head should be equal to m_events.size()

   public:
    Events() : m_head(0), m_tail(0) {}
    Events(const Events&) = delete;
    Events(Events&& other) {
        m_events    = std::move(other.m_events);
        m_lifetimes = std::move(other.m_lifetimes);
        m_head      = other.m_head;
        m_tail      = other.m_tail;

        other.m_head = other.m_tail;
    }
    Events& operator=(const Events&) = delete;
    Events& operator=(Events&& other) {
        m_events    = std::move(other.m_events);
        m_lifetimes = std::move(other.m_lifetimes);
        m_head      = other.m_head;
        m_tail      = other.m_tail;

        other.m_head = other.m_tail;
        return *this;
    }

    void push(const T& event) {
        m_events.emplace_back(event);
        m_lifetimes.emplace_back(1);
        m_tail++;
    }
    void push(T&& event) {
        m_events.emplace_back(std::move(event));
        m_lifetimes.emplace_back(1);
        m_tail++;
    }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events.emplace_back(std::forward<Args>(args)...);
        m_lifetimes.emplace_back(1);
        m_tail++;
    }
    void update() {
        while (!m_events.empty() && m_lifetimes.front() == 0) {
            m_events.pop_front();
            m_lifetimes.pop_front();
            m_head++;
        }
        std::ranges::for_each(m_lifetimes, [](std::uint32_t& lifetime) {
            if (lifetime > 0) {
                lifetime--;
            }
        });
    }
    void clear() {
        m_events.clear();
        m_lifetimes.clear();
        m_head = m_tail;
    }
    bool empty() const { return m_events.empty(); }
    size_t size() const { return m_events.size(); }
    std::uint32_t head() const { return m_head; }
    std::uint32_t tail() const { return m_tail; }
    T* get(std::uint32_t index) {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
    const T* get(std::uint32_t index) const {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
};

template <typename T>
struct EventCursor {
    std::uint32_t index = 0;
};
export template <typename T>
struct EventReader {
   private:
    Local<EventCursor<T>> _cursor;
    Res<Events<T>> _events;
    EventReader(Local<EventCursor<T>> cursor, Res<Events<T>> events) : _cursor(cursor), _events(events) {}

   public:
    static EventReader<T> from_param(Local<EventCursor<T>> cursor, Res<Events<T>> events) {
        cursor->index = std::max(cursor->index, events->head());
        cursor->index = std::min(cursor->index, events->tail());
        return EventReader<T>(cursor, events);
    }

    auto read() {
        return std::views::iota(_cursor->index, _events->tail()) |
               std::views::transform([this](std::uint32_t index) mutable {
                   _cursor->index++;
                   return *_events->get(index);
               });
    }
    auto read_with_id() {
        return std::views::iota(_cursor->index, _events->tail()) |
               std::views::transform([this](std::uint32_t index) mutable {
                   auto event = _events->get(index);
                   _cursor->index++;
                   return std::tuple<std::uint32_t, const T&>(index, *event);
               });
    }
    std::uint32_t size() const { return _events->tail() - _cursor->index; }
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

static_assert(from_param<EventReader<int>>);
static_assert(system_param<EventReader<int>>);

export template <typename T>
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
static_assert(from_param<EventWriter<int>>);
static_assert(system_param<EventWriter<int>>);
}  // namespace core