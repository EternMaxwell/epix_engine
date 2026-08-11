# Bundles

A bundle describes a fixed ordered group of components for spawning or
insertion. Most code can use the variadic APIs directly; the engine constructs
the corresponding bundle automatically.

```cpp
commands.spawn(Position{0, 0}, Velocity{1, 0}, Health{100});
world.spawn(Position{0, 0}, Velocity{1, 0});

auto entity = world.spawn_empty();
entity.insert(Position{}, Velocity{});
```

## `make_bundle`

Use `make_bundle` when an API specifically accepts one bundle object or when
component construction arguments should be explicit:

```cpp
auto values = make_bundle(Position{0, 0}, Velocity{1, 0});
world.spawn(std::move(values));

auto emplaced = make_bundle<Position, Health>(
    std::forward_as_tuple(4.0f, 8.0f),
    std::forward_as_tuple(100));
world.spawn(std::move(emplaced));
```

The first overload decays component values. The typed overload accepts one
argument tuple per component and constructs each value directly in ECS storage.
Bundles may be passed to `spawn`, `insert_bundle`, or
`insert_bundle_if_new`.

## Custom bundle specialization

`Bundle<T>` is an advanced customization point. A valid specialization exposes:

```cpp
static void get_components(
    T& bundle,
    utils::function_ref<void(utils::function_ref<void(void*)>)> write_component);

static /* range<optional<TypeId>> */ auto type_ids(const Components& components);
static /* range<TypeId> */ auto register_components(ComponentsRegistrator& components);
```

For every component, `get_components` passes a placement-construction callback
to `write_component`. The two returned ID ranges must have the same size and
order as those callbacks. `type_ids` may contain `std::nullopt` for a component
that has not been registered; `register_components` performs registration and
returns concrete IDs.

```cpp
static_assert(epix::ecs::is_bundle<MyBundle>);
```

Prefer `make_bundle` unless a reusable user-defined bundle type is essential.
Incorrect callback/ID ordering writes values into the wrong component storage.
The same component type must not occur twice in one bundle; use distinct wrapper
component types when two values have different meanings.
