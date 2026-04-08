# Component Hooks

Lifecycle callbacks invoked when a component is added, inserted, replaced, removed, or the entity is despawned.

## Overview

`ComponentHooks` stores up to five function pointers. They are registered per-component-type and called by the engine at the corresponding lifecycle event.

`HookContext` is passed to every hook:

```cpp
struct HookContext {
    Entity entity;      // entity being affected
    TypeId component_id; // type of the component being hooked
};
```

## Hook priority (highest first)

```
on_despawn → on_replace → on_remove → removed → added → on_add → on_insert
```

## Usage

### Registering hooks via static methods on the component type

The engine auto-discovers static `on_XYZ(World&, HookContext)` methods at component registration time:

```cpp
struct Collider {
    float radius;

    // Called when the component is first added (even if replacing)
    static void on_add(World& world, HookContext ctx) {
        // ctx.entity = the entity that just got a Collider
        world.resource_mut<PhysicsWorld>().register_body(ctx.entity);
    }

    // Called when the component is removed or the entity is despawned
    static void on_remove(World& world, HookContext ctx) {
        world.resource_mut<PhysicsWorld>().unregister_body(ctx.entity);
    }
};
```

The engine calls `ComponentHooks::update_from_component<T>()` automatically for `T` when the component is first registered. No explicit registration step is needed.

**Recognized static methods:**
- `static void on_add(World&, HookContext)`
- `static void on_insert(World&, HookContext)` — called every insert, including replacements
- `static void on_replace(World&, HookContext)` — called when an existing value is overwritten
- `static void on_remove(World&, HookContext)` — called on removal
- `static void on_despawn(World&, HookContext)` — called when the owning entity is despawned

### Registering hooks via `ComponentHooks` directly

If you cannot add static methods to a type, use the `Components` registry:

```cpp
app.world_mut().components_mut()
    .get_mut_or_register<MyType>()
    .hooks
    .try_on_add([](World& world, HookContext ctx) {
        // ...
    });
```

> ⚠ `try_on_XYZ` sets the hook only if it is **not already set** (returns `true` if set, `false` otherwise — note the inverted naming). If the component type defines the static method, that slot is already occupied.

## Constraints / Gotchas

- Hooks run **synchronously** inside the operation that triggered them (insert, remove, despawn). They have exclusive world access through the `World&` reference.
- Do not queue deferred commands inside hooks — the world is in a structural mutation state. Perform only immediate operations (read resources, insert/remove on other entities).
- `on_despawn` vs `on_remove`: `on_despawn` is triggered when the entity itself is despawned; `on_remove` is triggered when the component is explicitly removed from a live entity. Both fire when the entity is despawned if the component is present.
- `Parent` and `Children` use hooks internally to maintain hierarchy consistency. Do not override those component's hooks unless you replicate the hierarchy logic.
