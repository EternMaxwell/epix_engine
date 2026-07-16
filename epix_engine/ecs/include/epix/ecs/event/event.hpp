#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <epix/common.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
#endif

#include <epix/ecs/system/local.hpp>
#include <epix/ecs/system.hpp>
#include <epix/ecs/core/tick.hpp>
#include <epix/ecs/world.hpp>

namespace epix::ecs {
/** @brief Double-buffered event queue for type T.
 *  Events are kept alive for one update cycle after being pushed,
 *  then automatically expired and removed on the next update().
 *  @tparam T A movable event type. */
EPIX_EXPORT template <std::movable T>
struct Events {
   private:
    std::deque<T> m_events;  // event lifetime pair
    std::deque<std::uint32_t> m_lifetimes;
    std::uint32_t m_head;
    std::uint32_t m_tail;  // m_tail - m_head should be equal to m_events.size()

   public:
    Events() noexcept : m_head(0), m_tail(0) {}
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

    /** @brief Push a copy of an event into the queue. */
    void push(const T& event) {
        m_events.emplace_back(event);
        m_lifetimes.emplace_back(1);
        m_tail++;
    }
    /** @brief Push an event by move. */
    void push(T&& event) {
        m_events.emplace_back(std::move(event));
        m_lifetimes.emplace_back(1);
        m_tail++;
    }
    /** @brief Construct and push an event in-place. */
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events.emplace_back(std::forward<Args>(args)...);
        m_lifetimes.emplace_back(1);
        m_tail++;
    }
    /** @brief Tick all event lifetimes and remove expired events. */
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
    /** @brief Remove all events immediately. */
    void clear() {
        m_events.clear();
        m_lifetimes.clear();
        m_head = m_tail;
    }
    /** @brief Check if the queue contains no events. */
    bool empty() const noexcept { return m_events.empty(); }
    /** @brief Number of currently live events. */
    std::size_t size() const noexcept { return m_events.size(); }
    /** @brief Get the head (oldest live) event index. */
    std::uint32_t head() const noexcept { return m_head; }
    /** @brief Get the tail (next write) event index. */
    std::uint32_t tail() const noexcept { return m_tail; }
    /** @brief Get a mutable pointer to the event at the given index, or nullptr. */
    T* get(std::uint32_t index) noexcept {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
    /** @brief Get a const pointer to the event at the given index, or nullptr. */
    const T* get(std::uint32_t index) const noexcept {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
    /** @brief Advance the head past consumed events so later readers skip them. */
    void advance_head(std::uint32_t new_head) {
        new_head            = std::clamp(new_head, m_head, m_tail);
        std::uint32_t count = new_head - m_head;
        for (std::uint32_t i = 0; i < count; ++i) {
            m_events.pop_front();
            m_lifetimes.pop_front();
        }
        m_head = new_head;
    }
};

template <typename T>
struct EventCursor {
    std::uint32_t index = 0;
};
/** @brief System parameter that reads events of type T.
 *  Maintains a cursor so each reader only sees unread events.
 *  Usable directly as a system parameter. */
EPIX_EXPORT template <typename T>
struct EventReader {
   private:
    Local<EventCursor<T>> _cursor;
    Res<Events<T>> _events;
    EventReader(Local<EventCursor<T>> cursor, Res<Events<T>> events) noexcept : _cursor(cursor), _events(events) {}

   public:
    /** @brief Construct an EventReader from system parameters. */
    static EventReader<T> from_param(Local<EventCursor<T>> cursor, Res<Events<T>> events) {
        cursor->index = std::max(cursor->index, events->head());
        cursor->index = std::min(cursor->index, events->tail());
        return EventReader<T>(cursor, events);
    }

    /** @brief Return a range of unread events, advancing the cursor past them. */
    auto read() {
        return std::views::transform(std::views::iota(_cursor->index, _events->tail()),
                                     [this](std::uint32_t index) mutable {
                                         _cursor->index++;
                                         return *_events->get(index);
                                     });
    }
    /** @brief Return a range of (id, event) pairs for unread events. */
    auto read_with_id() {
        return std::views::transform(std::views::iota(_cursor->index, _events->tail()),
                                     [this](std::uint32_t index) mutable {
                                         auto event = _events->get(index);
                                         _cursor->index++;
                                         return std::tuple<std::uint32_t, const T&>(index, *event);
                                     });
    }
    /** @brief Number of events not yet consumed by this reader. */
    std::uint32_t size() const noexcept { return _events->tail() - _cursor->index; }
    /** @brief True if all events have been consumed. */
    bool empty() const noexcept { return _cursor->index == _events->tail(); }
    /** @brief Get the current read position (cursor index). */
    std::uint32_t position() const noexcept { return _cursor->index; }
    /** @brief Skip all unread events. */
    void clear() noexcept { _cursor->index = _events->tail(); }
    /** @brief Read exactly one event, or std::nullopt if none remain. */
    std::optional<std::reference_wrapper<const T>> read_one() {
        auto event = _events->get(_cursor->index);
        if (event) {
            _cursor->index++;
            return std::ref(*event);
        } else {
            return std::nullopt;
        }
    }
    /** @brief Read one event with its index, or std::nullopt if none remain. */
    std::optional<std::tuple<const T&, std::uint32_t>> read_one_index() {
        auto event = _events->get(_cursor->index);
        if (event) {
            std::uint32_t current_index = _cursor->index;
            _cursor->index++;
            return std::tuple<const T&, std::uint32_t>(*event, current_index);
        } else {
            return std::nullopt;
        }
    }
};

static_assert(internal::from_param<EventReader<int>>);
static_assert(system_param<EventReader<int>>);

/** @brief System parameter that writes events of type T.
 *  Wraps a mutable reference to Events<T>. */
EPIX_EXPORT template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>> m_events;
    EventWriter(ResMut<Events<T>> events) noexcept : m_events(events) {}

   public:
    /** @brief Construct an EventWriter from system parameters. */
    static EventWriter<T> from_param(ResMut<Events<T>> events) { return EventWriter<T>(events); }

    /** @brief Get the current write position (tail index). */
    std::uint32_t position() const noexcept { return m_events->tail(); }
    /** @brief Advance the head past consumed events so later readers skip them. */
    void advance_head(std::uint32_t new_head) { m_events->advance_head(new_head); }
    /** @brief Push an event by const reference. */
    void write(const T& event) { m_events->push(event); }
    /** @brief Push an event by move. */
    void write(T&& event) { m_events->push(std::move(event)); }
    /** @brief Construct and push an event in-place. */
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
    }
};
static_assert(internal::from_param<EventWriter<int>>);
static_assert(system_param<EventWriter<int>>);

EPIX_EXPORT struct RegisteredEvent {
    bool previously_updated;
    void (*update)(void*);
};
EPIX_EXPORT enum class UpdateState {
    Always,   // update every frame
    Waiting,  // wait until manually changed into ready, typically done by fixed update systems
    Ready,    // ready to be updated after waiting
};
EPIX_EXPORT struct EventRegistry {
    UpdateState state;
    std::unordered_map<TypeId, RegisteredEvent> events;

    template <std::movable T>
    static void register_event(World& world) {
        auto id         = world.init_resource<Events<T>>();
        auto&& registry = world.resource_or_init<EventRegistry>();
        registry.events.emplace(id, RegisteredEvent{.previously_updated = false, .update = [](void* queue) {
                                                        Events<T>* events = static_cast<Events<T>*>(queue);
                                                        events->update();
                                                    }});
    }
    void run_updates(World& world, Tick last_change_tick) {
        for (auto&& [id, e] : events) {
            Resources& resources = world.storage_mut().resources;
            auto& data           = resources.get_mut(id).value().get();
            TicksMut ticks   = TicksMut::from_refs(data.get_tick_refs().value(), last_change_tick, world.change_tick());
            bool has_changed = ticks.is_modified();
            if (e.previously_updated || has_changed) {
                auto* ptr = data.get_mut().value();
                e.update(ptr);
                e.previously_updated = has_changed || !e.previously_updated;
            }
        }
    }
    template <std::movable T>
    static void deregister_event(World& world) {
        auto id        = world.init_resource<Events<T>>();
        auto& registry = world.resource_or_init<EventRegistry>();
        registry.events.erase(id);
        world.remove_resource<Events<T>>();
    }
};

EPIX_EXPORT constexpr inline struct EventUpdateSystemT {
} EventUpdateSystem;

EPIX_EXPORT inline void signal_event_update(std::optional<ResMut<EventRegistry>> registry) {
    registry.transform([](ResMut<EventRegistry>& reg) {
        if (reg.get().state == UpdateState::Waiting) {
            reg.get_mut().state = UpdateState::Ready;
        }
        return 0;
    });
}
EPIX_EXPORT inline void event_update_system(World& world, Local<Tick> last_update_tick) {
    world.get_resource_mut<EventRegistry>().transform([&](EventRegistry& registry) {
        registry.run_updates(world, last_update_tick);
        if (registry.state == UpdateState::Ready) registry.state = UpdateState::Waiting;
        return 0;
    });
    last_update_tick.get() = world.change_tick();
}
EPIX_EXPORT inline bool event_update_condition(std::optional<ResMut<EventRegistry>> registry) {
    return registry.transform([](ResMut<EventRegistry>& reg) { return reg.get().state != UpdateState::Waiting; })
        .value_or(false);
}
}  // namespace epix::ecs
