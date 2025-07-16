#pragma once

#include "schedule.h"

namespace epix::app {
template <typename T>
struct Events {
   private:
    std::deque<std::pair<T, uint32_t>> m_events;  // event lifetime pair
    uint32_t m_head;
    uint32_t m_tail;  // m_tail - m_head should be equal to m_events.size()

   public:
    Events() : m_head(0), m_tail(0) {}
    Events(const Events&) = delete;
    Events(Events&& other) {
        m_events = std::move(other.m_events);
        m_head   = other.m_head;
        m_tail   = other.m_tail;
    }
    Events& operator=(const Events&) = delete;
    Events& operator=(Events&& other) {
        m_events = std::move(other.m_events);
        m_head   = other.m_head;
        m_tail   = other.m_tail;
        return *this;
    }

    void push(const T& event) {
        m_events.emplace_back(event, 1);
        m_tail++;
    }
    void push(T&& event) {
        m_events.emplace_back(std::move(event), 1);
        m_tail++;
    }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events.emplace_back(T(std::forward<Args>(args)...), 1);
        m_tail++;
    }
    void update() {
        while (!m_events.empty() && m_events.front().second == 0) {
            m_events.pop_front();
            m_head++;
        }
        for (auto& event : m_events) {
            if (event.second > 0) {
                event.second--;
            }
        }
    }
    void clear() {
        m_events.clear();
        m_head = m_tail;
    }
    bool empty() const { return m_events.empty(); }
    size_t size() const { return m_events.size(); }
    uint32_t head() const { return m_head; }
    uint32_t tail() const { return m_tail; }
    T* get(uint32_t index) {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head].first;
        }
        return nullptr;
    }
    const T* get(uint32_t index) const {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head].first;
        }
        return nullptr;
    }
};
template <typename T>
struct EventPointer {
    uint32_t index = 0;
};
template <typename T>
struct EventReader {
    struct iterator {
        uint32_t index;
        const Events<T>* events;
        iterator(uint32_t index, const Events<T>* events)
            : index(index), events(events) {}
        iterator& operator++() {
            index++;
            return *this;
        }
        bool operator==(const iterator& rhs) const {
            return index == rhs.index;
        }
        bool operator!=(const iterator& rhs) const {
            return index != rhs.index;
        }
        const T& operator*() {
            auto event = events->get(index);
            return *event;
        }
    };
    struct iterator_index {
        uint32_t index;
        const Events<T>* events;
        iterator_index(uint32_t index, const Events<T>* events)
            : index(index), events(events) {}
        iterator_index& operator++() {
            index++;
            return *this;
        }
        bool operator==(const iterator_index& rhs) const {
            return index == rhs.index;
        }
        bool operator!=(const iterator_index& rhs) const {
            return index != rhs.index;
        }
        std::pair<uint32_t, const T&> operator*() {
            auto event = events->get(index);
            return {index, *event};
        }
    };
    struct iterable {
       private:
        const uint32_t m_read_begin;
        const Events<T>* m_events;
        const iterator m_begin;
        const iterator m_end;

       public:
        iterable(uint32_t read_begin, const Events<T>* events)
            : m_read_begin(read_begin),
              m_events(events),
              m_begin(read_begin, events),
              m_end(events->tail(), events) {}
        iterator begin() { return m_begin; }
        iterator end() { return m_end; }
    };
    struct iterable_index {
       private:
        const uint32_t m_read_begin;
        const Events<T>* m_events;
        const iterator_index m_begin;
        const iterator_index m_end;

       public:
        iterable_index(uint32_t read_begin, const Events<T>* events)
            : m_read_begin(read_begin),
              m_events(events),
              m_begin(read_begin, events),
              m_end(events->tail(), events) {}
        iterator_index begin() { return m_begin; }
        iterator_index end() { return m_end; }
    };

   private:
    Local<EventPointer<T>>& m_pointer;
    Res<Events<T>>& m_events;
    EventReader(Local<EventPointer<T>>& pointer, Res<Events<T>>& events)
        : m_pointer(pointer), m_events(events) {}

   public:
    EventReader(const EventReader& other)
        : m_pointer(other.m_pointer), m_events(other.m_events) {}
    EventReader(EventReader&& other)
        : m_pointer(other.m_pointer), m_events(other.m_events) {}
    EventReader& operator=(const EventReader&) = delete;
    EventReader& operator=(EventReader&&)      = delete;

    static EventReader<T> from_param(Local<EventPointer<T>>& pointer,
                                     Res<Events<T>>& events) {
        pointer->index = std::max(pointer->index, events->head());
        pointer->index = std::min(pointer->index, events->tail());
        return EventReader<T>(pointer, events);
    }

    /**
     * @brief Iterating through events this reader has not yet read.
     *
     * @return `iterable` object that can be used to iterate through events.
     */
    auto read() {
        iterable iter(m_pointer->index, m_events);
        m_pointer->index = m_events->tail();
        return iter;
    }
    auto read_with_index() {
        iterable_index iter(m_pointer->index, m_events);
        m_pointer->index = m_events->tail();
        return iter;
    }
    /**
     * @brief Get the remaining events this reader has not yet read.
     *
     * @return `size_t` number of events remaining.
     */
    size_t size() const { return m_events->tail() - m_pointer->index; }
    /**
     * @brief Read the next event.
     *
     * @return `T*` pointer to the next event, or `nullptr` if there are no more
     */
    const T* read_one() {
        auto event = m_events->get(m_pointer->index);
        if (event) {
            m_pointer->index++;
            return event;
        } else {
            return nullptr;
        }
    }
    /**
     * @brief Read the next event and its index.
     *
     * @return `std::pair<uint32_t, T*>` pair of the index and pointer to the
     * next event, or {current_ptr, nullptr} if there are no more
     */
    std::pair<uint32_t, const T*> read_one_index() {
        auto pair =
            std::make_pair(m_pointer->index, m_events->get(m_pointer->index));
        if (pair.second) {
            m_pointer->index++;
        }
        return pair;
    }
    bool empty() const { return m_pointer->index == m_events->tail(); }
};
template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>>& m_events;
    EventWriter(ResMut<Events<T>>& events) : m_events(events) {}

   public:
    EventWriter(const EventWriter& other) : m_events(other.m_events) {}
    EventWriter(EventWriter&& other) : m_events(other.m_events) {}
    EventWriter& operator=(const EventWriter&) = delete;
    EventWriter& operator=(EventWriter&&)      = delete;

    static EventWriter<T> from_param(ResMut<Events<T>>& events) {
        return EventWriter<T>(events);
    }

    void write(const T& event) { m_events->push(event); }
    void write(T&& event) { m_events->push(std::move(event)); }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
    }
};
};  // namespace epix::app