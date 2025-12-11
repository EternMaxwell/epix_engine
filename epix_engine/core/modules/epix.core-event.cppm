/**
 * @file epix.core-event.cppm
 * @brief Event partition for event system
 */

export module epix.core:event;

import :fwd;
import :entities;
import :type_system;

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <vector>

export namespace epix::core::event {
    // Event storage
    template <typename E>
    struct Events {
       private:
        std::deque<E> events;
        size_t start_event_count = 0;
        
       public:
        void send(E event) {
            events.push_back(std::move(event));
        }
        
        auto iter() const {
            return events | std::views::drop(start_event_count);
        }
        
        void update() {
            size_t event_count = events.size();
            if (start_event_count > 0 && start_event_count == event_count) {
                events.clear();
                start_event_count = 0;
            } else {
                start_event_count = event_count;
            }
        }
        
        void clear() {
            events.clear();
            start_event_count = 0;
        }
    };
    
    // Event reader system parameter
    template <typename E>
    struct EventReader {
       private:
        const Events<E>* events;
        
       public:
        EventReader(const Events<E>* events) : events(events) {}
        
        auto iter() const {
            return events->iter();
        }
        
        auto read() const {
            return iter();
        }
    };
    
    // Event writer system parameter
    template <typename E>
    struct EventWriter {
       private:
        Events<E>* events;
        
       public:
        EventWriter(Events<E>* events) : events(events) {}
        
        void send(E event) {
            events->send(std::move(event));
        }
        
        void write(E event) {
            send(std::move(event));
        }
    };
}  // namespace epix::core::event
