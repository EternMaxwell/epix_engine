module;

export module epix.core:app.event;

import std;

import :system;
import :ticks;
import :world;

namespace epix::core {
/** @brief Double-buffered event queue for type T.
 *  Events are kept alive for one update cycle after being pushed,
 *  then automatically expired and removed on the next update().
 *  @tparam T A movable event type. */
export template <std::movable T>
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
    bool empty() const { return m_events.empty(); }
    /** @brief Number of currently live events. */
    std::size_t size() const { return m_events.size(); }
    /** @brief Get the head (oldest live) event index. */
    std::uint32_t head() const { return m_head; }
    /** @brief Get the tail (next write) event index. */
    std::uint32_t tail() const { return m_tail; }
    /** @brief Get a mutable pointer to the event at the given index, or nullptr. */
    T* get(std::uint32_t index) {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
    /** @brief Get a const pointer to the event at the given index, or nullptr. */
    const T* get(std::uint32_t index) const {
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
export template <typename T>
struct EventReader {
   private:
    Local<EventCursor<T>> _cursor;
    Res<Events<T>> _events;
    EventReader(Local<EventCursor<T>> cursor, Res<Events<T>> events) : _cursor(cursor), _events(events) {}

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
    std::uint32_t size() const { return _events->tail() - _cursor->index; }
    /** @brief True if all events have been consumed. */
    bool empty() const { return _cursor->index == _events->tail(); }
    /** @brief Get the current read position (cursor index). */
    std::uint32_t position() const { return _cursor->index; }
    /** @brief Skip all unread events. */
    void clear() { _cursor->index = _events->tail(); }
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

static_assert(from_param<EventReader<int>>);
static_assert(system_param<EventReader<int>>);

/** @brief System parameter that writes events of type T.
 *  Wraps a mutable reference to Events<T>. */
export template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>> m_events;
    EventWriter(ResMut<Events<T>> events) : m_events(events) {}

   public:
    /** @brief Construct an EventWriter from system parameters. */
    static EventWriter<T> from_param(ResMut<Events<T>> events) { return EventWriter<T>(events); }

    /** @brief Get the current write position (tail index). */
    std::uint32_t position() const { return m_events->tail(); }
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
static_assert(from_param<EventWriter<int>>);
static_assert(system_param<EventWriter<int>>);
}  // namespace epix::core