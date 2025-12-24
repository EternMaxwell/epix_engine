#pragma once

#include <algorithm>
#include <deque>

namespace epix::core::event {
template <typename T>
struct Events {
   private:
    std::deque<T> m_events;  // event lifetime pair
    std::deque<uint32_t> m_lifetimes;
    uint32_t m_head;
    uint32_t m_tail;  // m_tail - m_head should be equal to m_events.size()

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
        std::ranges::for_each(m_lifetimes, [](uint32_t& lifetime) {
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
    uint32_t head() const { return m_head; }
    uint32_t tail() const { return m_tail; }
    T* get(uint32_t index) {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
    const T* get(uint32_t index) const {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head];
        }
        return nullptr;
    }
};
}  // namespace epix::core::event