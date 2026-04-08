# TODO — core

Items that are designed but not yet fully implemented, based on source inspection.

---

## [~] Table-level query iteration (`set_table`)

**Files:** `epix_engine/core/modules/query/filter.cppm`, `query/fetch.cppm`

The `WorldQuery<T>` extension point has a `set_table` hook intended as a faster iteration path when every entity in an archetype lives in the same table (the common case for `Table`-storage components). Currently the hook is uniformly commented out across every built-in `WorldQuery<T>` specialization:

```cpp
// static void set_table(Fetch& fetch, State& state, const Table& table) {
//     fetch.table_dense = &table.get_dense(state.component_id).value().get();
// }
```

Affected specializations (representative list):
- `WorldQuery<With<T>>` — `filter.cppm`
- `WorldQuery<Without<T>>` — `filter.cppm`
- `WorldQuery<Or<Fs...>>` — `filter.cppm`
- `WorldQuery<std::tuple<Qs...>>` — `filter.cppm`
- `WorldQuery<Item<Qs...>>` — `filter.cppm`
- `WorldQuery<Entity>` — `filter.cppm`
- `WorldQuery<EntityLocation>` — `filter.cppm`
- `WorldQuery<Added<T>>` — `filter.cppm`
- `WorldQuery<const T&>`, `WorldQuery<T&>`, `WorldQuery<Opt<T&>>` etc. — `fetch.cppm`

**Impact:** The scheduler always calls `set_archetype` per archetype instead of the potentially cheaper per-table path. No correctness issue; only a performance opportunity.

**To implement:** Uncomment, verify, and add a corresponding `bool IS_DENSE` flag to `WorldQuery<T>` so the executor can choose the table path when all component storages are `Table`-type.

---

## [ ] Required-component registration API

**Files:** `epix_engine/core/modules/core/component.cppm`

The internal machinery for required components is complete: `RequiredComponents`, `RequiredComponent`, `RequiredComponentConstructor`, `Components::register_required`, `Components::register_required_by_id`, `Components::register_required_dyn`, and transitive/depth-aware inheritance. Bundle insertion already queries and applies required components at spawn/insert time.

What is **not** implemented:

1. **`App`-level shorthand.** There is no `App::register_required_component<Requiree, F>(F&&)` method. Users must reach into the world in a startup system:

   ```cpp
   void setup_requirements(World& world) {
       auto& components = world.components_mut();
       auto requiree = world.type_registry().type_id<Character>();
       components.register_required(requiree, [] { return Transform{}; });
   }
   app.add_systems(Startup, setup_requirements);
   ```

2. **No static method hook on component types.** Component hooks use a static method convention (`T::on_add`, `T::on_insert`, …) discovered by `ComponentHooks::update_from_component<T>()`. There is no equivalent for required components (e.g. `static void require_components(Components&)` read during `Components::register_info<T>()`).

**To implement (option A — static method hook):**

Add a `if constexpr (requires { T::require_components(components); }) { T::require_components(components); }` call inside `Components::register_info<T>()`. Component types can then declare their requirements via:

```cpp
struct Character {
    static void require_components(Components& c) {
        c.register_required(c.registry().type_id<Character>(), [] { return Transform{}; });
        c.register_required(c.registry().type_id<Character>(), [] { return Visibility{true}; });
    }
};
```

**To implement (option B — `App` method):**

```cpp
template <typename Requiree, typename F>
App& register_required_component(F&& constructor) {
    world().components_mut().register_required(
        world().type_registry().type_id<Requiree>(), std::forward<F>(constructor));
    return *this;
}
```
