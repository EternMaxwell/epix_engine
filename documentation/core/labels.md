# Labels

Strongly-typed identifiers for schedules, system sets, and sub-applications.

## Overview

`Label` is a generic key composed of a `meta::type_index` and an optional `uintptr_t` extra discriminator. Three distinct subtypes provide type-safe wrappers:

| Type             | Used for                                 |
| ---------------- | ---------------------------------------- |
| `SystemSetLabel` | Identifies a set inside a schedule       |
| `ScheduleLabel`  | Identifies a schedule inside `Schedules` |
| `AppLabel`       | Identifies a sub-app                     |

All three are constructible from the same set of inputs as `Label`.

## Usage

### From an empty tag type (preferred for named sets/schedules)

```cpp
struct MySchedule {};
struct Physics {};

app.add_systems(MySchedule{}, into(physics_system));
app.configure_sets(Update, sets(Physics{}).before(Render{}));
```

The label identity comes from the type, not the value. Two objects of the same empty type produce the same label.

### From an enum (for numbered or state-keyed labels)

```cpp
enum class RenderSet { Prepare, Draw, Present };

app.configure_sets(Render,
    sets(RenderSet::Prepare, RenderSet::Draw, RenderSet::Present).chain());
app.add_systems(Render, into(draw_sprites).in_set(RenderSet::Draw));
```

Enum labels encode both the enum type and the enumerator value, so `RenderSet::Draw` ≠ `RenderSet::Present`.

### From an integral

```cpp
ScheduleLabel sched_5 = ScheduleLabel::from_integral(5);
```

### From a pointer

```cpp
Label ptr_label = Label::from_pointer(&my_object);
```

### String representation

```cpp
ScheduleLabel lbl(MySchedule{});
std::println("{}", lbl.to_string()); // "MySchedule#0"
```

## `Label` construction rules

A value `t` of type `T` is accepted by `Label(T)` when:
- `T` is an enum type → encoded as `(type_id<T>, static_cast<uintptr_t>(t))`
- `T` is a pointer → encoded as `(type_id<T>, (uintptr_t)t)`
- `T` is an integral → encoded as `(type_id<T>, static_cast<uintptr_t>(t))`
- `T` is an empty type → encoded as `(type_id<T>, 0)`

Two labels are equal iff both their `type_index` and `extra` match.

## Constraints / Gotchas

- `SystemSetLabel`, `ScheduleLabel`, and `AppLabel` are different types. A `ScheduleLabel` value is **not** equal to a `SystemSetLabel` value even if constructed from the same enum.
- Empty types produce the same label regardless of which object you use — this is the intended pattern for singleton schedule labels (e.g., `Update`, `Startup`).
- Labels are value types with trivial copy and hash. Store them freely.
