# ECS Module

`epix.ecs` is the data and scheduling foundation of Epix Engine. It is usable
without `epix.app`; the application module adds runners, plugins, built-in
schedule labels, states, sub-apps, and extraction.

## Core Parts

- [`World`](world.md): entities, components, canonical entity-backed resources,
  archetypes, and immediate mutation.
- [`Schedule`](schedule.md): dependency-ordered system collections with
  configurable executors and deferred-command handling.
- [System params](system-params.md): `Res<T>`, `ResMut<T>`, `Local<T>`,
  `Commands`, `World&`, queries, events, and removal readers.
- [`Query`](query.md) and [query built-ins](query-built-ins.md): typed component
  access, filters, optional data, and change-aware references.
- [`Events`](events.md): double-buffered event queues with independent readers.
- [Resources](resources.md): singleton values implemented in normal entity
  component storage.
- [Removed components](removed-components.md): observe removal, despawn, and
  resource removal through an independent system cursor.
- [`Bundle`](bundle.md), [hierarchy](hierarchy.md), [change detection](change-detection.md),
  [component hooks](component-hooks.md), and [labels](labels.md).

## Standalone World

```cpp
import epix.ecs;

using namespace epix::ecs;

struct Position { float x, y; };
struct Velocity { float x, y; };

int main() {
    World world{WorldId{1}};
    Entity entity = world.spawn(Position{}, Velocity{1.0f, 2.0f}).id();

    auto query = world.query_filtered<Item<Entity, Position&>, With<Velocity>>();
    for (auto&& [id, position] : query.iter(world)) {
        position.x += 1.0f;
    }

    world.entity_mut(entity).remove<Velocity>();
}
```

For scheduled applications, import `epix.app` too and continue with
[the application quick reference](../app/quick.md).
