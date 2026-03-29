module;

export module epix.core:app.state;

import std;

import :app.decl;
import :ticks;

namespace epix::core {
/** @brief Read-only state resource wrapping an enum value.
 *  Only the App can modify the underlying value; systems read it via Res<State<T>>.
 *  @tparam T An enum type representing application states. */
export template <typename T>
    requires std::is_enum_v<T>
struct State {
   protected:
    /** @brief The current state value. */
    T m_state;
    State& operator=(T state) {
        m_state = state;
        return *this;
    }

    friend struct ::epix::core::App;

   public:
    State() : m_state() {}
    State(T state) : m_state(state) {}
    State(const State&)            = delete;
    State(State&&)                 = default;
    State& operator=(const State&) = delete;
    State& operator=(State&&)      = default;
    ~State()                       = default;
    /** @brief Implicitly convert to the underlying enum value. */
    operator T() const { return m_state; }
    /** @brief Check equality with a state value. */
    bool operator==(T state) const { return m_state == state; }
    /** @brief Check inequality with a state value. */
    bool operator!=(T state) const { return m_state != state; }

    /** @brief Check if the current state equals the given value. */
    bool is_state(T state) const { return m_state == state; }
};
/** @brief Mutable state resource that allows systems to request state transitions.
 *  Write to this via ResMut<NextState<T>> to trigger a transition.
 *  @tparam T An enum type representing application states. */
export template <typename T>
    requires std::is_enum_v<T>
struct NextState : public State<T> {
   public:
    using State<T>::State;

    /** @brief Implicitly convert to the underlying enum value. */
    operator T() const { return State<T>::m_state; }
    /** @brief Check equality with a state value. */
    bool operator==(T state) const { return State<T>::m_state == state; }
    /** @brief Check inequality with a state value. */
    bool operator!=(T state) const { return State<T>::m_state != state; }
    /** @brief Assign a new state value directly. */
    NextState& operator=(T state) {
        State<T>::m_state = state;
        return *this;
    }

    /** @brief Request a state transition to the given value. */
    void set_state(T state) { State<T>::m_state = state; }
};
/** @brief A predicate for checking if the application is in a specific state.
 *  Used for system run conditions, e.g. `run_if(in_state(AppState::Playing))`. */
export template <typename T>
    requires std::is_enum_v<T>
struct in_state {
   private:
    T m_state;

   public:
    in_state(T state) : m_state(state) {}
    /** @brief Evaluate whether the current state matches the target. */
    bool operator()(Res<State<T>> state) const { return *state == m_state; }
};
}  // namespace core