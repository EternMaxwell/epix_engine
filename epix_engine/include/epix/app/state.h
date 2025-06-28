#pragma once

#include "schedule.h"

namespace epix::app {
template <typename T>
    requires std::is_enum_v<T>
struct State {
    friend struct App;

   protected:
    T m_state;
    bool just_created = true;
    State& operator=(T state) {
        m_state      = state;
        just_created = false;
        return *this;
    }

   public:
    State() : m_state() {}
    State(T state) : m_state(state) {}
    operator T() const { return m_state; }
    bool operator==(T state) const { return m_state == state; }
    bool operator!=(T state) const { return m_state != state; }
    bool is_just_created() const { return just_created; }

    friend struct App;
};
template <typename T>
    requires std::is_enum_v<T>
struct NextState : public State<T> {
   public:
    using State<T>::State;

    operator T() const { return State<T>::m_state; }
    bool operator==(T state) const { return State<T>::m_state == state; }
    bool operator!=(T state) const { return State<T>::m_state != state; }
    NextState& operator=(T state) {
        State<T>::m_state = state;
        return *this;
    }

    void set_state(const T& state) { State<T>::m_state = state; }
};
}  // namespace epix::app