# Built-in Schedules

The predefined schedule labels installed by `MainSchedulePlugin` (included in `App::create()`).

## Overview

epix_engine provides a fixed set of schedule labels that run each frame in a guaranteed order. Every schedule always runs; the startup group runs once, then is removed from the order.

## Execution Order

**Startup group** (run once at app start):
```
PreStartup → Startup → PostStartup
```

**Per-frame group** (run every frame):
```
First → PreUpdate → Update → PostUpdate → Last
```

**State transitions** (run each frame, interleaved at `StateTransition`):
```
StateTransition  (inside the per-frame group)
```

**Exit group** (run on shutdown):
```
PreExit → Exit → PostExit
```

## Usage

```cpp
app.add_systems(Startup,    into(init_game));
app.add_systems(PreUpdate,  into(read_input));
app.add_systems(Update,     into(physics, ai));
app.add_systems(PostUpdate, into(camera_follow));
app.add_systems(Last,       into(flush_render_commands));
app.add_systems(Exit,       into(save_game));
```

## `ScheduleInfo`

`ScheduleInfo` extends `ScheduleLabel` with a list of per-set transforms applied to every `SetConfig` added under that label. Used primarily by the state machine API (`OnEnter`, `OnExit`, `OnChange`) to inject run conditions automatically.

```cpp
ScheduleInfo info = OnEnter(AppState::Playing);
app.add_systems(info, into(start_music));
// Equivalent to:
app.add_systems(StateTransition,
    into(start_music).run_if([...state check...]).in_set(StateTransitionSet::Callback));
```

## State Transition Schedules

Three schedule-factory functors produce `ScheduleInfo` values that run inside `StateTransition`:

| Functor                 | When it runs                                        |
| ----------------------- | --------------------------------------------------- |
| `OnEnter(State::Value)` | The frame `State<T>` is added or changed to `Value` |
| `OnExit(State::Value)`  | The frame `State<T>` changes away from `Value`      |
| `OnChange(/* any */)`   | Any frame `State<T>` is modified                    |

```cpp
app.add_systems(OnEnter(AppState::Playing), into(on_start));
app.add_systems(OnExit(AppState::Playing),  into(on_leave));
app.add_systems(OnChange(AppState{}),       into(on_any_transition));
```

## `ScheduleOrder`

The `ScheduleOrder` resource controls which schedules `App::update()` runs and in what order.

```cpp
app.schedule_order().insert_after(PostUpdate, MyCustomSchedule);
app.schedule_order().insert_begin(EarlyBootstrap);
```

Custom schedules not in `ScheduleOrder` must be run explicitly:
```cpp
app.run_schedule(MyCustomSchedule);
```

## Built-in Schedule Singleton Objects

Each label is an inline singleton of a unique tag type for type-safe use as a label:

```cpp
// Type        | Singleton object
PreStartupT     PreStartup
StartupT        Startup
PostStartupT    PostStartup
FirstT          First
PreUpdateT      PreUpdate
UpdateT         Update
PostUpdateT     PostUpdate
LastT           Last
PreExitT        PreExit
ExitT           Exit
PostExitT       PostExit
StateTransitionT StateTransition
```

Pass them directly to `add_systems` / `configure_sets` / `run_schedule`.

## Constraints / Gotchas

- `MainSchedulePlugin` is installed by `App::create()`. If you build `App` manually, you must install it yourself.
- The `StateTransition` schedule is separate from the per-frame schedules. It is placed inside the schedule order at a fixed position by `MainSchedulePlugin`.
- `OnEnter` and `OnExit` detect changes by checking whether `State<T>` is added or modified in the current tick. If state is inserted and immediately changed in the same frame before `StateTransition` runs, only the final value is seen.
