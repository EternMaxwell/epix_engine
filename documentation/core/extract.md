# Extract

Redirect a system parameter to read from the main (extracted) world inside a render sub-app.

## Overview

`Extract<T>` is a thin wrapper around any `system_param T`. When a system runs inside a sub-app (e.g. a render world), `Extract<T>` fetches `T`'s data from the main world stored in the `ExtractedWorld` resource, rather than from the current sub-app world.

This is the primary mechanism for render systems to read gameplay data without synchronization issues.

**Constraint:** `T` must not be a deferred parameter (no commands, no `Deferred<>`). A deferred `T` inside `Extract<T>` throws at initialization.

## Usage

### Setting up extraction in the App

```cpp
AppaLabel RenderApp;

app.add_sub_app(RenderApp);
app.get_sub_app_mut(RenderApp)->set_extract_fn(
    [](App& render_app, World& main_world) {
        render_app.world_mut().insert_resource(
            ExtractedWorld{std::ref(main_world)});
    }
);
```

### Using `Extract<T>` in a render system

```cpp
void render_sprites(
    Extract<Query<Item<Entity, const Position&, const Sprite&>>> query
) {
    for (auto&& [e, pos, sprite] : query.iter()) {
        // 'pos' and 'sprite' come from the main world
        draw(pos, sprite);
    }
}

app.get_sub_app_mut(RenderApp)->add_systems(Render, into(render_sprites));
```

### Mixing Extract and non-extracted params

```cpp
void upload_to_gpu(
    Extract<Query<Item<const Mesh&>>>  meshes,   // reads main world
    ResMut<GpuBuffer>                  buffer    // reads render world
) {
    for (auto&& [mesh] : meshes.iter()) {
        buffer->upload(mesh);
    }
}
```

Non-`Extract` parameters still access the sub-app's own world.

## Constraints / Gotchas

- `ExtractedWorld` must be present as a resource in the sub-app's world before any system with `Extract<T>` runs. The extract function (set via `App::set_extract_fn`) is responsible for inserting it each frame.
- Deferred parameters inside `Extract` are not allowed. `Extract<Commands>` will throw at startup.
- Access-conflict checking for `Extract<T>` uses the main world's type registry but registers the conflict in the sub-app's access set — so an `Extract<Res<T>>` and a sub-app-local `ResMut<T>` will not conflict (they are different worlds).
- `Extract<T>` inherits all of `T`'s operations via public inheritance. You can call any method available on `T` directly on the `Extract<T>` value.
