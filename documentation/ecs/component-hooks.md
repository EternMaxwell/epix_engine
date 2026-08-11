# Component hooks

Component hooks are synchronous lifecycle callbacks registered with component
metadata. The engine discovers recognized static methods when a component type
is first registered.

```cpp
struct Collider {
    float radius;

    static void on_add(World& world, HookContext context) {
        world.resource_mut<PhysicsWorld>().register_body(context.entity);
    }

    static void on_remove(World& world, HookContext context) {
        world.resource_mut<PhysicsWorld>().unregister_body(context.entity);
    }
};
```

`HookContext` contains the affected `entity` and `component_id`.

## Recognized callbacks

- `on_add(World&, HookContext)` runs when the component is first added.
- `on_insert(World&, HookContext)` runs after insertion, including replacement.
- `on_replace(World&, HookContext)` runs before an existing value is replaced.
- `on_remove(World&, HookContext)` runs when the component is removed.
- `on_despawn(World&, HookContext)` runs as the owning entity is despawned.

No explicit registration call is required for static component methods.
`ComponentHooks` and its `try_on_*` functions are part of the metadata
implementation, but the current public `Components` API does not expose a
general mutable hook-registration path for types that cannot define static
methods.

Hooks execute inside structural ECS operations with exclusive `World&` access.
Keep them small and avoid retaining references into component storage across
other structural mutations. `Parent` and `Children` use hooks to maintain the
hierarchy; their behavior depends on those built-in callbacks.
