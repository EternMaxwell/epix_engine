# Change detection

Every component and resource stores `added` and `modified` ticks. A system
compares them with its last-run tick through `Ref<T>`, `Mut<T>`, `Res<T>`,
`ResMut<T>`, or the `Added<T>` and `Modified<T>` filters.

## Query references

```cpp
void report(Query<Item<Entity, Ref<Position>>> query) {
    for (auto&& [entity, position] : query.iter()) {
        if (position.is_added()) { /* newly inserted */ }
        if (position.is_modified()) { /* changed since this system last ran */ }
        std::println("{}: {}, {}", entity.index, position->x, position->y);
    }
}
```

`Ref<T>` provides const access plus `is_added()`, `is_modified()`,
`last_modified()`, and `added_tick()`.

Use `Mut<T>` for tracked mutable access:

```cpp
void clamp(Query<Item<Mut<Position>>> query) {
    for (auto&& [position] : query.iter()) {
        if (position->x > 100.0f) position->x = 100.0f;
    }
}
```

Non-const `operator->`, `operator*`, `get_mut()`, `ptr_mut()`, and conversion to
`T&` mark the value modified before returning mutable access. Const operations
do not. `Mut<T>` intentionally has no public `set_modified()` or `set_added()`;
the underlying `TicksMut` helper owns those low-level operations.

## Resources and filters

`Res<T>` derives from `Ref<T>` and `ResMut<T>` derives from `Mut<T>`, so the same
tick queries apply to resources.

```cpp
void react(Res<GameConfig> config) {
    if (config.is_modified()) reload_config(*config);
}

void changed_health(Query<Item<Entity, const Health&>, Modified<Health>> query) {
    for (auto&& [entity, health] : query.iter()) { /* ... */ }
}
```

`Added<T>` and `Modified<T>` are non-archetypal filters: matching component
storage is selected by archetype, then each row's tick is tested.

## Tick behavior

`Tick` uses wrapping 32-bit arithmetic. Newly inserted data has both its added
and modified tick set, so both predicates initially report true. The world
periodically clamps excessively old ticks through `check_change_tick()`;
`App::update()` performs that maintenance automatically.
