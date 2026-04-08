# Bundle

A customization point for grouping multiple components into a single insertable unit.

## Overview

`Bundle<T>` is a template struct that you specialize to teach the engine how to write a user type's components into entity storage. Any type with a valid `Bundle<T>` specialization satisfies the `is_bundle` concept and can be used anywhere components are accepted.

## Usage

### Spawning with multiple components (no bundle needed)

```cpp
// Components can be passed directly — the engine packs them automatically
commands.spawn(Position{0,0}, Velocity{1,0}, Health{100});
world.spawn(Position{0,0}, Velocity{1,0});
```

This uses the built-in variadic-component bundle implicitly.

### Defining a named bundle

Define a struct and specialize `Bundle<T>`:

```cpp
struct Transform {
    glm::vec3 position{};
    glm::quat rotation{};
    glm::vec3 scale{1,1,1};
};

// Specialize Bundle<Transform> to tell the engine what components it writes
template <>
struct epix::core::Bundle<Transform> {
    // Must return the number of components written
    static std::size_t write(Transform& b, std::span<void*> ptrs) {
        new (ptrs[0]) glm::vec3(b.position);
        new (ptrs[1]) glm::quat(b.rotation);
        new (ptrs[2]) glm::vec3(b.scale);
        return 3;
    }
    // Must return the TypeId for each component in the same order as write()
    static auto type_ids(const TypeRegistry& reg) {
        return std::array{
            reg.type_id<glm::vec3>(),  // position
            reg.type_id<glm::quat>(),  // rotation
            reg.type_id<glm::vec3>(),  // scale — same type, different component!
        };
    }
    // Must register component metadata
    static void register_components(const TypeRegistry& reg, Components& components) {
        components.register_component<glm::vec3>(reg);
        components.register_component<glm::quat>(reg);
    }
};
```

> ⚠ No usage example found in tests/examples for custom Bundle specialization via the raw API. The built-in `InitializeBundle` helper is recommended instead (see below).

### Using `InitializeBundle` (recommended)

`InitializeBundle<Ts, ArgTuples>` autogenerates a `Bundle` for a struct that is just a group of simpler components, without having to write `write()` and `type_ids()` manually.

> ⚠ The primary `Bundle<T>` customization via `InitializeBundle` is used internally. The public user pattern is to rely on variadic `spawn(...)` for small ad-hoc groups and to define named struct bundles with a manual `Bundle<T>` specialization for reuse.

### Checking the concept

```cpp
static_assert(epix::core::is_bundle<Transform>);
```

## Extending / Custom Implementations

Required interface for `Bundle<T>`:

| Static method         | Signature                                                    | Description                                                                          |
| --------------------- | ------------------------------------------------------------ | ------------------------------------------------------------------------------------ |
| `write`               | `std::size_t write(T&, std::span<void*>)`                    | Write components into uninitialized storage pointers in order. Return count written. |
| `type_ids`            | `auto type_ids(const TypeRegistry&)`                         | Return a range of `TypeId` in the same order as `write`.                             |
| `register_components` | `void register_components(const TypeRegistry&, Components&)` | Register all component types with the engine.                                        |

## Constraints / Gotchas

- The order of `type_ids()` **must** match the order of `write()`. Mismatch leads to undefined behavior.
- Components that appear multiple times in a bundle (same type) are not supported — the second write will overwrite the first. Use distinct wrapper types instead.
- `register_components` is called before any entities are spawned with this bundle. The engine caches bundle info keyed by the set of component type ids.
