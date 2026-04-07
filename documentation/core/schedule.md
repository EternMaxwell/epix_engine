# Schedule

Named, dependency-ordered system collections with optional parallel execution.

## Overview

A `Schedule` holds a directed graph of system sets (`SetConfig` nodes). Systems are ordered via `after()`, `before()`, and `in_set()` relationships. Execution is driven by one of the available executors (default: `AutoExecutor`).

`App` stores schedules in the `Schedules` resource and runs them in the order defined by `ScheduleOrder` each time `App::update()` is called.

## Usage

### Adding systems to a schedule via App

```cpp
// Wrap one or more system functions into a SetConfig:
app.add_systems(Update, into(my_system));
app.add_systems(Update, into(sys_a, sys_b));          // independent, may parallelize
app.add_systems(Update, into(sys_a, sys_b).chain());  // sys_a → sys_b
app.add_systems(Update, into(sys_a).after(sys_b));    // explicit ordering
app.add_systems(Update, into(sys).run_if(some_condition));
```

`into(...)` requires all arguments to be system functions returning `void` with no explicit input.
`sets(...)` is the same but allows label-only sets (no system required, used for `configure_sets`).

### Configuring sets without a system (ordering groups)

```cpp
enum class RenderSet { Prepare, Draw, Present };

app.configure_sets(Render,
    sets(RenderSet::Prepare, RenderSet::Draw, RenderSet::Present).chain());

app.add_systems(Render, into(draw_sprites).in_set(RenderSet::Draw));
```

### Run conditions

```cpp
app.add_systems(Update, into(game_tick).run_if(in_state(AppState::Playing)));

// Custom predicate:
app.add_systems(Update, into(debug_system).run_if(
    [](Res<DebugFlags> flags) { return flags->show_debug; }
));
```

### Chaining

```cpp
app.add_systems(Update, into(step_a, step_b, step_c).chain());
// Equivalent to: step_a → step_b → step_c
```

### Using Schedule directly (tests / custom runners)

```cpp
World world(WorldId(1));
Schedule sched(Update);

sched.add_systems(into(spawn_entities));
sched.add_systems(into(query_system).after(spawn_entities));

sched.prepare();            // validate deps, build execution cache
sched.initialize_systems(world);
sched.execute(world);
```

### Schedule configuration

```cpp
ScheduleConfig cfg;
cfg.executor_config.deferred = DeferredApply::ApplyEnd; // default
cfg.run_once = false;
// Optional loop condition (repeats the system body while true):
cfg.loop_condition = [](World& w) { return w.resource<Counter>().value < 100; };

sched.with_schedule_config(cfg);
```

### Executors

| Type                         | Description                                      |
| ---------------------------- | ------------------------------------------------ |
| `AutoExecutor`               | Default: automatically selects strategy          |
| `MultithreadFlatExecutor`    | Flat-graph parallel (recommended for most games) |
| `TaskflowExecutor`           | Taskflow work-stealing scheduler                 |
| `MultithreadClassicExecutor` | Thread-pool classic                              |
| `SingleThreadExecutor`       | Single-threaded, for debugging                   |

```cpp
sched.with_executor(std::make_unique<executors::TaskflowExecutor>());
```

## `SetConfig` API

| Method               | Effect                                               |
| -------------------- | ---------------------------------------------------- |
| `.after(label)`      | This set runs after `label`                          |
| `.before(label)`     | This set runs before `label`                         |
| `.in_set(label)`     | This set is a child of the set identified by `label` |
| `.run_if(condition)` | Add a run condition                                  |
| `.chain()`           | Sub-items run in order                               |
| `.set_name("name")`  | Display name for debugging                           |

## `DeferredApply` values

| Value           | Behavior                                   |
| --------------- | ------------------------------------------ |
| `ApplyDirect`   | Apply after each system                    |
| `QueueDeferred` | Queue for later batch apply                |
| `ApplyEnd`      | Apply all at end of schedule (**default**) |
| `Ignore`        | Do not apply deferred commands             |

## Constraints / Gotchas

- `into(...)` requires every argument to be a `void`-returning system with no explicit `In<T>` input. Use `sets(...)` for label-only set configuration.
- A `SetConfig` wrapping a single lambda creates a `SystemSetLabel` from the lambda's type. Two different lambdas that look identical are still distinct types — keep the lambda alive if you need to reference it via a label.
- `prepare()` must be called before `execute()`. `App` calls it automatically as part of `add_systems`.
- Systems in the same parallel tier that have conflicting resource/component accesses will be automatically serialized by the executor.
- `SchedulePrepareError::ParentsWithDeps` is returned when a set has two parents that are also ordered relative to each other — this is a structural error that must be resolved.
