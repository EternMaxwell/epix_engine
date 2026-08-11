# Extract

`Extract<T>` redirects an ECS system parameter to the source world's data while
the system runs in a sub-app. Rendering uses this temporary bridge to transfer
main-world state into render-owned storage.

## Setting up a sub-app

```cpp
struct RenderAppTag {};
AppLabel render_label{RenderAppTag{}};
inline struct ExtractScheduleTag {} ExtractSchedule;

App& render_app = app.sub_app_or_insert(render_label);
render_app
    .add_schedule(Schedule{ExtractSchedule})
    .set_extract_fn([](App& sub_app, World&) {
        sub_app.run_schedule(ExtractSchedule);
    });
```

`App::extract(source_app)` temporarily inserts `ExtractedWorld` into the
destination world, calls the configured extract function, and removes the
temporary resource afterward. Runners that pipeline sub-apps call this method
at the synchronization boundary.

The built-in renderer creates this setup automatically under `render::Render`.

## Using `Extract<T>`

```cpp
void extract_sprites(
    Extract<Query<Item<Entity, const Position&, const Sprite&>>> sprites,
    ResMut<ExtractedSprites> destination)
{
    for (auto&& [entity, position, sprite] : sprites.iter()) {
        destination->update(entity, position, sprite);
    }
}

render_app.add_systems(ExtractSchedule, into(extract_sprites));
```

`Extract<Query<...>>` reads the source world while the unwrapped
`ResMut<ExtractedSprites>` accesses the sub-app world. The same pattern works
for resources, events, and other non-deferred system parameters. `Extract<T>`
preserves the wrapped parameter's mutability: `Extract<ResMut<T>>` is valid and
mutates the source-world resource. Rendering code usually reads or takes data
and stores independent render-world state rather than retaining aliases.

## Constraints

- The destination app must be running inside `App::extract()` so its temporary
  `ExtractedWorld` resource exists.
- Deferred parameters are rejected. `Extract<Commands>` and
  `Extract<Deferred<...>>` throw during system initialization.
- Extracted and sub-app-local access belong to different worlds. They may refer
  to the same C++ type without representing the same storage.
- The source-world reference exists only during `App::extract()`. Do not retain
  references or parameter items after the extract system returns.
- `Extract<T>` publicly inherits the fetched `T` item, so its normal operations
  are available directly.
