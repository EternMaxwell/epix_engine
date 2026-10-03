#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;

using namespace epix::ecs;

namespace {
struct TableA {};
struct V {
    std::string s;
    V(std::string_view sv) : s(sv) {}
    static inline int inserted = 0;
    static inline int replaced = 0;
    static void on_insert(World&, HookContext) { ++inserted; }
    static void on_replace(World&, HookContext) { ++replaced; }
};
struct TableB {};
struct SparseA {};
struct SparseB {};
struct SparseV {
    std::string s;
    SparseV(std::string_view sv) : s(sv) {}
};
}  // namespace

template <>
struct epix::ecs::sparse_component<SparseA> : std::true_type {};
template <>
struct epix::ecs::sparse_component<SparseB> : std::true_type {};
template <>
struct epix::ecs::sparse_component<SparseV> : std::true_type {};

namespace {
void replace_every_v(Commands commands, Query<Item<Entity, const V&>> query) {
    std::vector<std::pair<Entity, V>> batch;
    for (auto&& [entity, value] : query.iter()) {
        batch.emplace_back(entity, V("two"));
    }
    commands.try_insert_batch(batch);
}

struct SeenModified {
    std::vector<Entity> entities;
};
struct SeenAdded {
    std::vector<Entity> entities;
};
void collect_modified(Query<Item<Entity, const V&>, Modified<V>> query, ResMut<SeenModified> seen) {
    seen->entities.clear();
    for (auto&& [entity, value] : query.iter()) {
        seen->entities.push_back(entity);
    }
}
void collect_added(Query<Item<Entity, const V&>, Added<V>> query, ResMut<SeenAdded> seen) {
    seen->entities.clear();
    for (auto&& [entity, value] : query.iter()) {
        seen->entities.push_back(entity);
    }
}
struct SparseSeenModified {
    std::vector<Entity> entities;
};
struct SparseSeenAdded {
    std::vector<Entity> entities;
};
void collect_sparse_modified(Query<Item<Entity, const SparseV&>, Modified<SparseV>> query,
                             ResMut<SparseSeenModified> seen) {
    seen->entities.clear();
    for (auto&& [entity, value] : query.iter()) {
        seen->entities.push_back(entity);
    }
}
void collect_sparse_added(Query<Item<Entity, const SparseV&>, Added<SparseV>> query, ResMut<SparseSeenAdded> seen) {
    seen->entities.clear();
    for (auto&& [entity, value] : query.iter()) {
        seen->entities.push_back(entity);
    }
}
}  // namespace

// bevy_ecs/src/bundle/tests.rs:203-215 `insert_if_new`
TEST(ecs, insert_if_new_keeps_existing_table_value) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(V("one"));
    auto entity = spawned.id();

    auto mut = world.get_entity_mut(entity).value();
    mut.insert_if_new(V("two"));
    mut.insert_bundle_if_new(make_bundle<TableA, V>(std::make_tuple(), std::make_tuple("three")));

    auto ref = world.get_entity(entity).value();
    EXPECT_TRUE(ref.contains<TableA>());
    EXPECT_TRUE(ref.contains<V>());
    EXPECT_EQ(ref.get<V>()->get().s, "one");
}

// bevy_ecs/src/bundle/tests.rs:225-237 `sparse_set_insert_if_new`
TEST(ecs, insert_if_new_keeps_existing_sparse_value) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(SparseV("one"));
    auto entity = spawned.id();

    auto mut = world.get_entity_mut(entity).value();
    mut.insert_if_new(SparseV("two"));
    mut.insert_bundle_if_new(make_bundle<SparseA, SparseV>(std::make_tuple(), std::make_tuple("three")));

    auto ref = world.get_entity(entity).value();
    EXPECT_TRUE(ref.contains<SparseA>());
    EXPECT_TRUE(ref.contains<SparseV>());
    EXPECT_EQ(ref.get<SparseV>()->get().s, "one");
}

TEST(ecs, insert_replaces_existing_table_value) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(V("one"));
    auto entity = spawned.id();

    auto mut = world.get_entity_mut(entity).value();
    mut.insert(V("two"));

    auto ref = world.get_entity(entity).value();
    EXPECT_EQ(ref.get<V>()->get().s, "two");

    // the same archetype is reached through the archetype move as well
    mut.insert_bundle(make_bundle<TableA, V>(std::make_tuple(), std::make_tuple("three")));
    ref = world.get_entity(entity).value();
    EXPECT_EQ(ref.get<V>()->get().s, "three");
}

TEST(ecs, insert_replaces_existing_sparse_value) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(SparseV("one"));
    auto entity = spawned.id();

    auto mut = world.get_entity_mut(entity).value();
    mut.insert(SparseV("two"));

    auto ref = world.get_entity(entity).value();
    EXPECT_EQ(ref.get<SparseV>()->get().s, "two");

    mut.insert_bundle(make_bundle<SparseA, SparseV>(std::make_tuple(), std::make_tuple("three")));
    ref = world.get_entity(entity).value();
    EXPECT_EQ(ref.get<SparseV>()->get().s, "three");
}

TEST(ecs, emplace_replaces_existing_value) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(V("one"));
    auto entity = spawned.id();

    auto mut = world.get_entity_mut(entity).value();
    mut.emplace<V>(std::forward_as_tuple("two"));
    EXPECT_EQ(world.get_entity(entity).value().get<V>()->get().s, "two");

    mut.emplace_if_new<V>(std::forward_as_tuple("three"));
    EXPECT_EQ(world.get_entity(entity).value().get<V>()->get().s, "two");
}

// The bundle status vector is zipped against the bundle write order, so a bundle
// that mixes existing and new components must still write every component.
TEST(ecs, insert_aligns_statuses_with_bundle_write_order) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(V("one"));
    spawned.insert(SparseV("one"));
    auto entity = spawned.id();

    auto mut = world.get_entity_mut(entity).value();
    mut.insert_bundle(make_bundle<TableA, V, TableB>(std::make_tuple(), std::make_tuple("two"), std::make_tuple()));
    mut.insert_bundle(
        make_bundle<SparseA, SparseV, SparseB>(std::make_tuple(), std::make_tuple("two"), std::make_tuple()));

    auto ref = world.get_entity(entity).value();
    EXPECT_TRUE(ref.contains<TableA>());
    EXPECT_TRUE(ref.contains<TableB>());
    EXPECT_TRUE(ref.contains<SparseA>());
    EXPECT_TRUE(ref.contains<SparseB>());
    EXPECT_EQ(ref.get<V>()->get().s, "two");
    EXPECT_EQ(ref.get<SparseV>()->get().s, "two");

    mut.insert_bundle_if_new(
        make_bundle<TableA, V, TableB>(std::make_tuple(), std::make_tuple("three"), std::make_tuple()));
    mut.insert_bundle_if_new(
        make_bundle<SparseA, SparseV, SparseB>(std::make_tuple(), std::make_tuple("three"), std::make_tuple()));
    ref = world.get_entity(entity).value();
    EXPECT_EQ(ref.get<V>()->get().s, "two");
    EXPECT_EQ(ref.get<SparseV>()->get().s, "two");
}

// bevy_ecs/src/bundle/tests.rs:116-135 `component_hook_order_replace`
TEST(ecs, insert_mode_component_hooks) {
    World world(WorldId(1));
    V::inserted = 0;
    V::replaced = 0;

    auto spawned = world.spawn();
    spawned.insert(V("one"));
    auto entity = spawned.id();
    const int inserted_by_spawn = V::inserted;

    auto mut = world.get_entity_mut(entity).value();
    mut.insert_if_new(V("two"));
    EXPECT_EQ(V::replaced, 0);
    EXPECT_EQ(V::inserted, inserted_by_spawn);
    EXPECT_EQ(world.get_entity(entity).value().get<V>()->get().s, "one");

    mut.insert(V("three"));
    EXPECT_EQ(V::replaced, 1);
    EXPECT_EQ(V::inserted, inserted_by_spawn + 1);
    EXPECT_EQ(world.get_entity(entity).value().get<V>()->get().s, "three");
}

TEST(ecs, try_insert_batch_replaces_value) {
    World world(WorldId(1));
    auto spawned = world.spawn();
    spawned.insert(V("one"));
    auto entity = spawned.id();

    Schedule schedule(ScheduleLabel::from_type<V>());
    schedule.add_systems(into(replace_every_v));
    ASSERT_TRUE(schedule.prepare(true).has_value());
    schedule.initialize_systems(world);
    schedule.execute(world);

    EXPECT_EQ(world.get_entity(entity).value().get<V>()->get().s, "two");
}

// The insert modes drive the change-detection ticks the same way Bevy's do:
// `Component::initialize` sets the added and changed ticks together
// (bevy_ecs/src/storage/table/column.rs:185-199) while `replace` sets only the
// changed tick (column.rs:208-221). `Modified<T>` is this port's `Changed<T>`,
// so it matches a freshly added component exactly as Bevy's `Changed<T>` does
// (bevy_ecs/src/system/mod.rs:542-568).
TEST(ecs, insert_mode_drives_change_detection) {
    World world(WorldId(1));
    world.insert_resource(SeenModified{});
    world.insert_resource(SeenAdded{});
    world.insert_resource(SparseSeenModified{});
    world.insert_resource(SparseSeenAdded{});
    auto spawned = world.spawn();
    spawned.insert(V("one"));
    auto entity = spawned.id();

    auto modified        = make_system_unique(collect_modified);
    auto added           = make_system_unique(collect_added);
    auto sparse_modified = make_system_unique(collect_sparse_modified);
    auto sparse_added    = make_system_unique(collect_sparse_added);
    modified->initialize(world);
    added->initialize(world);
    sparse_modified->initialize(world);
    sparse_added->initialize(world);
    const auto run_all = [&] {
        EXPECT_TRUE(modified->run({}, world).has_value());
        EXPECT_TRUE(added->run({}, world).has_value());
        EXPECT_TRUE(sparse_modified->run({}, world).has_value());
        EXPECT_TRUE(sparse_added->run({}, world).has_value());
        return std::tuple{world.resource<SeenModified>().entities, world.resource<SeenAdded>().entities,
                          world.resource<SparseSeenModified>().entities,
                          world.resource<SparseSeenAdded>().entities};
    };

    // a component that was just added is both changed and added
    auto [first_modified, first_added, first_sparse_modified, first_sparse_added] = run_all();
    EXPECT_EQ(first_modified, (std::vector{entity}));
    EXPECT_EQ(first_added, (std::vector{entity}));
    EXPECT_TRUE(first_sparse_modified.empty());
    EXPECT_TRUE(first_sparse_added.empty());

    // nothing inserted in between: neither filter matches again
    auto [idle_modified, idle_added, idle_sparse_modified, idle_sparse_added] = run_all();
    EXPECT_TRUE(idle_modified.empty());
    EXPECT_TRUE(idle_added.empty());
    EXPECT_TRUE(idle_sparse_modified.empty());
    EXPECT_TRUE(idle_sparse_added.empty());

    // `insert` replaces the value: changed, but not added again
    world.entity_mut(entity).insert(V("two"));
    auto [replaced_modified, replaced_added, replaced_sparse_modified, replaced_sparse_added] = run_all();
    EXPECT_EQ(replaced_modified, (std::vector{entity}));
    EXPECT_TRUE(replaced_added.empty());
    EXPECT_TRUE(replaced_sparse_modified.empty());
    EXPECT_TRUE(replaced_sparse_added.empty());

    // `insert_if_new` keeps the existing value and leaves both ticks untouched
    world.entity_mut(entity).insert_if_new(V("three"));
    auto [kept_modified, kept_added, kept_sparse_modified, kept_sparse_added] = run_all();
    EXPECT_TRUE(kept_modified.empty());
    EXPECT_TRUE(kept_added.empty());
    EXPECT_TRUE(kept_sparse_modified.empty());
    EXPECT_TRUE(kept_sparse_added.empty());
    EXPECT_EQ(world.get_entity(entity).value().get<V>()->get().s, "two");

    // the same holds for sparse-set storage
    world.entity_mut(entity).insert(SparseV("one"));
    auto [sparse_unused_modified, sparse_unused_added, sparse_insert_modified, sparse_insert_added] = run_all();
    EXPECT_TRUE(sparse_unused_modified.empty());
    EXPECT_TRUE(sparse_unused_added.empty());
    EXPECT_EQ(sparse_insert_modified, (std::vector{entity}));
    EXPECT_EQ(sparse_insert_added, (std::vector{entity}));

    // `insert` on a sparse-set component replaces the value: changed, not added
    world.entity_mut(entity).insert(SparseV("two"));
    auto [sparse_replaced_unused, sparse_replaced_unused_added, sparse_replaced_modified, sparse_replaced_added] =
        run_all();
    EXPECT_TRUE(sparse_replaced_unused.empty());
    EXPECT_TRUE(sparse_replaced_unused_added.empty());
    EXPECT_EQ(sparse_replaced_modified, (std::vector{entity}));
    EXPECT_TRUE(sparse_replaced_added.empty());

    // `insert_if_new` on a sparse-set component keeps the value and both ticks
    world.entity_mut(entity).insert_if_new(SparseV("three"));
    auto [sparse_kept_unused, sparse_kept_unused_added, sparse_kept_modified, sparse_kept_added] = run_all();
    EXPECT_TRUE(sparse_kept_unused.empty());
    EXPECT_TRUE(sparse_kept_unused_added.empty());
    EXPECT_TRUE(sparse_kept_modified.empty());
    EXPECT_TRUE(sparse_kept_added.empty());
    EXPECT_EQ(world.get_entity(entity).value().get<SparseV>()->get().s, "two");
}
