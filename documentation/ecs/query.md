# Query

`Query<D, F>` iterates entities whose components satisfy data descriptor `D`
and filter `F`. Data is usually an `Item<...>`; the default `Filter<>` matches
all archetypes compatible with the data.

```cpp
void move_entities(
    Query<Item<Entity, Position&, const Velocity&>, With<Movable>> query)
{
    for (auto&& [entity, position, velocity] : query.iter()) {
        position.x += velocity.x;
        position.y += velocity.y;
    }
}
```

## Optional and direct lookup

```cpp
void inspect(Query<Item<const Position&, Opt<const Health&>>> query,
             Res<Target> target) {
    if (auto item = query.get(target->entity)) {
        auto&& [position, health] = *item;
    }
}
```

`Opt<T>` makes one data item optional without filtering out the entity.
`get(entity)` uses the entity location and returns empty when the entity is
invalid, its archetype does not match, or a runtime filter rejects it.
`as_readonly()` produces the read-only projection of a mutable query.

## First match and `Single`

```cpp
if (auto first = query.single()) {
    // the first match; additional matches are not checked
}

void camera(Single<Item<Position&>, With<MainCamera>> camera) {
    camera->x = 0.0f;
}
```

Despite its name, current `Single<D, F>` validation requires at least one match
and then stores the first. It does not prove uniqueness. The system is skipped
only when the associated query is empty.

## Filters

```cpp
Query<Item<const Position&>, With<Player>>
Query<Item<const Position&>, Without<Invisible>>
Query<Item<const Position&>, Or<With<Player>, With<Npc>>>
Query<Item<const Position&>, std::tuple<With<Player>, Without<Dead>>>
```

Filters in a tuple are ANDed. See [Query built-ins](query-built-ins.md) for
change filters and optional/change-aware data.

## Extension points

Advanced query descriptors specialize three customization points:

- `WorldQuery<T>` defines fetch/state creation, archetype binding, component
  access, registration lookup, and component-set matching.
- `QueryData<T>` defines the yielded `Item`, read-only projection, mutability,
  and row fetch.
- `QueryFilter<T>` defines per-row filtering and whether the filter is purely
  archetypal.

These are low-level storage interfaces. Use the current declarations in
`epix/ecs/query/fetch.hpp` and the built-in specializations in
`epix/ecs/query/refs.hpp` as the contract when implementing one; old
`TypeRegistry`-based examples do not apply to the current ECS.

Conflicting query/system-parameter access is rejected during system
initialization. Iterating an empty query is valid.
