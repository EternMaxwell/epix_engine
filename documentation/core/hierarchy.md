# Hierarchy

Parent–child entity relationships with automatic propagation.

## Overview

`Parent` and `Children` are ordinary components with lifecycle hooks. When you insert `Parent{entity}` on an entity, the hook automatically adds the child to the parent's `Children` set. When an entity with `Children` is despawned, all child entities are recursively despawned.

## Usage

### Creating a parent–child relationship

```cpp
// Via Commands (recommended in systems):
void spawn_tree(Commands cmd) {
    auto parent = cmd.spawn(Position{0, 0}).id();

    // Attaching a child — insert Parent directly:
    cmd.spawn(Position{1, 0}, Parent{parent});

    // Or use EntityCommands::spawn for syntactic sugar:
    cmd.entity(parent)
       .spawn(Position{2, 0})   // spawns with Parent{parent} automatically
       .spawn(Position{3, 0});
}

// Via World directly (e.g. in tests):
auto parent_e = world.spawn(Position{0,0}).id();
world.spawn(Position{1,0}, Parent{parent_e});
```

### Querying children

```cpp
void iterate_children(
    Query<Item<Entity, const Children&>> parents,
    Query<Item<const Position&>> all
) {
    for (auto&& [parent_id, children] : parents.iter()) {
        for (Entity child : children.entities()) {
            if (auto opt = all.get(child); opt) {
                auto& [pos] = *opt;
                std::println("child pos: ({}, {})", pos.x, pos.y);
            }
        }
    }
}
```

### Walking up the hierarchy

```cpp
void find_root(
    Query<Item<Entity, const Parent&>> with_parent,
    Query<Item<const Parent&>> parent_query
) {
    for (auto&& [e, parent] : with_parent.iter()) {
        Entity root = e;
        while (true) {
            if (auto opt = parent_query.get(root); opt) {
                root = std::get<0>(*opt).entity();
            } else {
                break;
            }
        }
        std::println("Entity {}'s root: {}", e.index, root.index);
    }
}
```

## Lifecycle Hooks

`Parent` and `Children` register component hooks that maintain consistency:

| Hook                   | Trigger                             | Effect                                                              |
| ---------------------- | ----------------------------------- | ------------------------------------------------------------------- |
| `Parent::on_insert`    | `Parent` added to entity            | Adds child to parent's `Children` set; creates `Children` if absent |
| `Parent::on_remove`    | `Parent` removed from entity        | Removes child from parent's `Children` set                          |
| `Children::on_despawn` | Entity with `Children` is despawned | Recursively despawns all child entities                             |

You do not need to manage `Children` directly — it is maintained automatically.

## Constraints / Gotchas

- `Children` is not meant to be inserted manually. Insert `Parent{id}` on the child; `Children` is created automatically on the parent.
- Despawning a parent despawns all children recursively. To detach a child without despawning it, remove its `Parent` component first.
- Circular parent chains are not detected and will cause infinite recursion during despawn.
- `Children::entities()` returns an `unordered_set<Entity>` — order is unspecified.
