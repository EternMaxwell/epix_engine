#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <deque>
#include <epix/common.hpp>
#include <utility>
#endif

namespace epix::ecs {

EPIX_EXPORT template <std::movable T>
struct Events {
   private:
    std::deque<T> m_events;
    std::deque<std::uint32_t> m_lifetimes;
    std::uint32_t m_head;
    std::uint32_t m_tail;

   public:
    Events() noexcept : m_head(0), m_tail(0) {}
    Events(const Events&) = delete;
    Events(Events&& other)
        : m_events(std::move(other.m_events)),
          m_lifetimes(std::move(other.m_lifetimes)),
          m_head(other.m_head),
          m_tail(other.m_tail) {
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
            if (lifetime > 0) lifetime--;
        });
    }
    void clear() {
        m_events.clear();
        m_lifetimes.clear();
        m_head = m_tail;
    }
    bool empty() const noexcept { return m_events.empty(); }
    std::size_t size() const noexcept { return m_events.size(); }
    std::uint32_t head() const noexcept { return m_head; }
    std::uint32_t tail() const noexcept { return m_tail; }
    T* get(std::uint32_t index) noexcept {
        return index >= m_head && index < m_tail ? &m_events[index - m_head] : nullptr;
    }
    const T* get(std::uint32_t index) const noexcept {
        return index >= m_head && index < m_tail ? &m_events[index - m_head] : nullptr;
    }
    void advance_head(std::uint32_t new_head) {
        new_head = std::clamp(new_head, m_head, m_tail);
        for (std::uint32_t i = 0; i < new_head - m_head; ++i) {
            m_events.pop_front();
            m_lifetimes.pop_front();
        }
        m_head = new_head;
    }
};

EPIX_EXPORT template <typename T>
struct EventCursor {
    std::uint32_t index = 0;
};

}  // namespace epix::ecs
