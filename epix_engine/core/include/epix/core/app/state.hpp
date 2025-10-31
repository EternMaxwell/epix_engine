#pragma once

#include <type_traits>

#include "../fwd.hpp"

namespace epix::core::app {
template <typename T>
    requires std::is_enum_v<T>
struct State {
   protected:
    T m_state;
    State& operator=(T state) {
        m_state = state;
        return *this;
    }

    friend struct epix::core::App;

   public:
    State() : m_state() {}
    State(T state) : m_state(state) {}
    State(const State&)            = delete;
    State(State&&)                 = default;
    State& operator=(const State&) = delete;
    State& operator=(State&&)      = default;
    ~State()                       = default;
    operator T() const { return m_state; }
    bool operator==(T state) const { return m_state == state; }
    bool operator!=(T state) const { return m_state != state; }

    bool is_state(T state) const { return m_state == state; }
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

    void set_state(T state) { State<T>::m_state = state; }
};
}  // namespace epix::core::app