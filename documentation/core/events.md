# Events

Double-buffered event queue: `Events<T>`, `EventReader<T>`, `EventWriter<T>`.

## Overview

Events provide a one-to-many communication channel between systems within the same frame.  Events live for exactly one update cycle: they are available during the frame they were sent and the next frame, then automatically expired by the `Events<T>::update()` call (run in the `Last` schedule when `add_event<T>()` is used).

## Usage

### Register an event type

```cpp
app.add_event<MyEvent>();
// Creates Events<MyEvent> resource and schedules automatic update() in Last
```

### Produce events

```cpp
struct DamageEvent { Entity target; int amount; };

void attack_system(Query<Item<Entity, const Attack&>> query,
                   EventWriter<DamageEvent> events) {
    for (auto&& [e, atk] : query.iter()) {
        events.write(DamageEvent{atk.target, atk.damage});
    }
}
```

`EventWriter<T>` methods:
- `write(const T&)` / `write(T&&)` — push an event
- `emplace(args...)` — construct in-place
- `position()` — current tail index
- `advance_head(idx)` — mark events before `idx` as consumed for all readers

### Consume events

```cpp
void health_system(EventReader<DamageEvent> events,
                   Query<Item<Health&>, With<Entity>> query) {
    for (auto evt : events.read()) {
        if (auto opt = query.get(evt.target); opt) {
            std::get<0>(*opt).value -= evt.amount;
        }
    }
}
```

`EventReader<T>` methods:
- `read()` — range of unread events (advances cursor)
- `read_with_id()` — range of `(index, event)` pairs
- `read_one()` — `std::optional<std::reference_wrapper<const T>>`
- `read_one_index()` — `std::optional<std::tuple<const T&, uint32_t>>`
- `size()` — number of unread events
- `empty()` — true if no unread events
- `clear()` — skip remaining unread events (does not remove from queue)

Each `EventReader` has an independent cursor; multiple readers do not interfere.

### Manually managing `Events<T>`

In rare cases (custom timing, tests), access the resource directly:

```cpp
void manual_events(ResMut<Events<MyEvent>> events) {
    events->push(MyEvent{42});
    events->update();   // expire old events
    events->clear();    // remove all events immediately
}
```

## Constraints / Gotchas

- `add_event<T>()` must be called before any system that uses `EventReader<T>` or `EventWriter<T>`. Call it in a plugin's `build()`.
- Events are expired after `update()` is called (automatically in `Last` via `add_event`). A reader that misses the event frame will see nothing.
- `EventReader::read()` consumes the events for that reader (advances its cursor). Calling `read()` again on the same reader in the same system returns an empty range.
- `EventReader` is a `from_param` system parameter — the cursor state is stored in a `Local<EventCursor<T>>`, so it persists across frames correctly.
