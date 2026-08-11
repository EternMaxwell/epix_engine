# Removed Components

`RemovedComponents<T>` is a read-only system parameter that reports entities
whose `T` component was removed or whose entity was despawned. Resource removal
is reported too because resources are entity-backed components.

```cpp
void report_removed(RemovedComponents<Velocity> removed) {
    for (Entity entity : removed.read()) {
        std::println("Velocity removed from {}", entity.uid);
    }
}
```

Each system instance owns an independent cursor. Reading advances only that
system's cursor; it does not consume events for other systems.

| Method | Meaning |
| --- | --- |
| `read()` | Iterate unread entity IDs and advance the cursor. |
| `read_with_id()` | Iterate `(Entity, event_id)` tuples. |
| `len()` / `size()` | Number of unread removals. |
| `is_empty()` / `empty()` | Whether the reader has unread removals. |
| `clear()` | Advance this reader to the stream tail. |
| `events()` | Access the underlying removal-event stream, if allocated. |

For direct world access, use `world.removed<T>()` or
`world.removed_with_id(component_id)`. These return removals recorded since the
latest tracker clear.

Removal records are emitted after `on_remove` hooks. Replacing an existing
component is not a removal. `App::update()` advances the buffers through
`World::clear_trackers()`; unread events remain available to an existing reader
for the following clear boundary and then expire.
