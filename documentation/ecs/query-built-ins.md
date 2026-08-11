# Query built-ins

The common query data descriptors are component references plus `Entity`,
`EntityLocation`, `const Archetype&`, `Opt<T>`, `Ref<T>`, and `Mut<T>`.

## Identity and location

```cpp
void inspect(Query<Item<Entity, EntityLocation, const Archetype&, const Position&>> query) {
    for (auto&& [entity, location, archetype, position] : query.iter()) {
        // entity: stable generational ID
        // location: current archetype and table row
        // archetype: metadata for the matched archetype
    }
}
```

These descriptors are read-only and do not add component access.

## Optional components

`Opt<const T&>` or `Opt<T&>` does not require the component to be present:

```cpp
void maybe_health(Query<Item<Entity, Opt<const Health&>>> query) {
    for (auto&& [entity, health] : query.iter()) {
        if (health) std::println("{}", health->get().value);
    }
}
```

The fetched item is an `std::optional<std::reference_wrapper<...>>`. Mutable
optional access counts as a write for conflict detection.

## Change-aware references

`Ref<T>` is immutable; `Mut<T>` is mutable and marks the component modified
when mutable access is requested.

```cpp
void move_changed(Query<Item<Mut<Position>>> query) {
    for (auto&& [position] : query.iter()) {
        if (position.is_added()) initialize(position.get_mut());
        position->x += 1.0f;
    }
}
```

Both expose `is_added()`, `is_modified()`, `last_modified()`, and
`added_tick()`. See [Change detection](change-detection.md).

## Filters

Filters occupy the second `Query<D, F>` template argument:

| Filter | Meaning | Archetypal |
| --- | --- | --- |
| `With<Ts...>` | all listed components are present | yes |
| `Without<Ts...>` | all listed components are absent | yes |
| `Or<Fs...>` | at least one nested filter matches | depends on nested filters |
| `Added<T>` | component was added since the system last ran | no |
| `Modified<T>` | component was modified since the system last ran | no |

```cpp
Query<Item<Entity, const Health&>,
      std::tuple<With<Player>, Without<Dead>, Modified<Health>>>
```

Access conflicts are detected when the system/query is initialized. For
example, `Mut<T>` cannot coexist with another query parameter that reads or
writes the same component unless the accesses are separated with a supported
mechanism such as `ParamSet`.
