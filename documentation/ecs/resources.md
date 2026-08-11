# Resources

Resources are per-type singleton values exposed through `World`, `Res<T>`, and
`ResMut<T>`. They now use the same archetype/sparse-set component storage as
ordinary entities instead of a separate resource store.

## Canonical Resource Entity

Registering a resource type adds an `IsResource` requirement. The first valid
resource instance becomes the type's canonical resource entity and is cached by
`ResourceEntities`.

```cpp
World world{WorldId{1}};
world.insert_resource(Settings{.volume = 0.8f});

Entity entity = world.resource_entity<Settings>().value();
auto ref = world.entity(entity);
assert(ref.contains<Settings>());
assert(ref.contains<IsResource>());
```

The ordinary resource API remains the preferred interface:

```cpp
world.init_resource<Settings>();
world.emplace_resource<NonCopyableSettings>(42);

const Settings& settings = world.resource<Settings>();
world.resource_mut<Settings>().volume = 0.5f;

auto taken = world.take_resource<Settings>();
bool removed = world.remove_resource<NonCopyableSettings>();
```

## Consequences

- Resource access and `Query` access to the same component type address the
  same storage, so the scheduler rejects conflicting access.
- Direct entity mutation of the canonical resource is visible through
  `resource<T>()` and participates in change detection.
- Removing the resource component leaves its canonical entity available for a
  later insertion. Removing `IsResource` or despawning that entity clears its
  canonical-resource registration.
- `clear_entities()` preserves resource entities and values;
  `clear_resources()` removes their resource values while preserving the
  canonical entities.
- Resource types must be movable. A type already registered as an ordinary
  component cannot later be registered as a resource.

Only one entity may own a resource type. Spawning a duplicate component after a
canonical resource exists does not create a second resource.
