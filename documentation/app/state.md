# State

Finite-state-machine support: `State<T>`, `NextState<T>`, `in_state`, and `OnEnter`/`OnExit`/`OnChange` schedule triggers.

## Overview

States are enum-based resources with two halves:
- `State<T>` — the **current** state, read-only to systems
- `NextState<T>` — the **pending** state, writable by systems

`App` copies `NextState<T>` into `State<T>` each frame during `StateTransition`, then fires the appropriate `OnEnter`/`OnExit`/`OnChange` schedules.

## Usage

### Register a state

```cpp
enum class AppState { Loading, Menu, Playing, Paused };

app.insert_state(AppState::Loading);  // also installs the StateTransition system
// or, for default-constructed:
app.init_state<AppState>();
```

### Read the current state

```cpp
void my_system(Res<State<AppState>> state) {
    if (*state == AppState::Playing) { /* ... */ }
    if (state.is_added())    { /* first frame this state was created */ }
    if (state.is_modified()) { /* state just transitioned */ }
}
```

`State<T>` also implicitly converts to `T`:
```cpp
void check(Res<State<AppState>> state) {
    AppState current = *state;
}
```

### Trigger a state transition

```cpp
void request_pause(ResMut<NextState<AppState>> next, Res<PauseRequested> pause) {
    if (pause->value) {
        *next = AppState::Paused;
        // or: next->set_state(AppState::Paused);
    }
}
```

`NextState<T>` is moved into `State<T>` automatically during `StateTransition`.

### Run conditions

```cpp
// System only runs when the state is AppState::Playing
app.add_systems(Update,
    into(game_tick).run_if(in_state(AppState::Playing)));
```

`in_state<T>` is a callable struct:
```cpp
struct in_state<AppState> {
    AppState m_state;
    bool operator()(Res<State<AppState>> state) const { return *state == m_state; }
};
```

### Transition hooks via OnEnter / OnExit

```cpp
app.add_systems(OnEnter(AppState::Playing), into(load_level));
app.add_systems(OnExit(AppState::Playing),  into(unload_level));
app.add_systems(OnChange(AppState{}),       into(log_transition));
```

These install systems in the `StateTransition` schedule with the appropriate run conditions.

## Constraints / Gotchas

- `State<T>` exposes the current value but no public mutation operation. Request
  transitions through `ResMut<NextState<T>>`.
- The transition from `NextState<T>` to `State<T>` happens in `StateTransition`. Systems examining `State<T>` before `StateTransition` runs see the **previous** value.
- Only enum types (`std::is_enum_v<T>`) are accepted as state types.
- `insert_state` is idempotent for the same type — a second call after the first has no effect (the resource already exists).
