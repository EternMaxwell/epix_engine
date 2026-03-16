module;

#include <gtest/gtest.h>

export module epix.core:tests.bundle_inserter;

import std;
import :bundle;
import :world;

using namespace core;

namespace {
struct Y {
    std::string s;
    Y(std::string_view sv) : s(sv) {}
};
struct X {
    int v;
    X(int vv) : v(vv) {}
};
struct Z {
    double d;
    Z(double dd) : d(dd) {}
};
struct W {
    std::string s;
    W(std::string_view sv) : s(sv) {}
};
}  // namespace

template <>
struct core::sparse_component<Z> : std::true_type {};
template <>
struct core::sparse_component<W> : std::true_type {};

TEST(core, bundle_inserter) {
    auto registry = std::make_shared<TypeRegistry>();
    World world(WorldId(1), registry);

    // register component infos
    TypeId tx = registry->type_id<X>();
    TypeId ty = registry->type_id<Y>();
    world.components_mut().register_info<X>();
    world.components_mut().register_info<Y>();

    // create a bundle type and register
    using MyBundle = InitializeBundle<std::tuple<X, Y>, std::tuple<std::tuple<int>, std::tuple<std::string_view>>>;
    auto spawner   = BundleSpawner::create<MyBundle>(world, 123);

    // reserve and spawn an entity
    Entity e = world.entities_mut().alloc();
    spawner.reserve_storage(1);
    auto loc = spawner.spawn_non_exist(e, make_bundle<X, Y>(std::forward_as_tuple(11), std::forward_as_tuple("hi")));
    auto table_id_before = loc.table_id;
    EXPECT_EQ(e.generation, 0);
    EXPECT_EQ(e.index, 0);
    EXPECT_EQ(loc.archetype_id.get(), 1);  // archetype 1: X,Y
    EXPECT_EQ(loc.archetype_idx.get(), 0);
    EXPECT_EQ(loc.table_id.get(), 1);
    EXPECT_EQ(loc.table_idx.get(), 0);
    // check data
    {
        auto& table = world.storage_mut().tables.get_mut(loc.table_id).value().get();
        auto& x     = table.get_dense(tx).value().get().get_as<X>(loc.table_idx).value().get();
        auto& y     = table.get_dense(ty).value().get().get_as<Y>(loc.table_idx).value().get();
        EXPECT_EQ(x.v, 11);
        EXPECT_EQ(y.s, "hi");
    }

    auto inserter = BundleInserter::create<
        InitializeBundle<std::tuple<Z, W>, std::tuple<std::tuple<double>, std::tuple<std::string_view>>>>(
        world, loc.archetype_id, 123);
    loc = inserter.insert(e, loc, make_bundle<Z, W>(std::forward_as_tuple(3.14), std::forward_as_tuple("hello")), true);
    EXPECT_EQ(loc.archetype_id.get(), 2);  // archetype 2: X,Y,Z,W
    EXPECT_EQ(loc.archetype_idx.get(), 0);
    EXPECT_EQ(loc.table_id.get(), 1);
    EXPECT_EQ(loc.table_idx.get(), 0);
    // check data
    {
        auto& table2 = world.storage_mut().tables.get_mut(loc.table_id).value().get();
        auto& x2     = table2.get_dense(registry->type_id<X>()).value().get().get_as<X>(loc.table_idx).value().get();
        auto& y2     = table2.get_dense(registry->type_id<Y>()).value().get().get_as<Y>(loc.table_idx).value().get();
        auto& z2 =
            world.storage_mut().sparse_sets.get_mut(registry->type_id<Z>()).value().get().get_as<Z>(e).value().get();
        auto& w2 =
            world.storage_mut().sparse_sets.get_mut(registry->type_id<W>()).value().get().get_as<W>(e).value().get();
        EXPECT_EQ(x2.v, 11);
        EXPECT_EQ(y2.s, "hi");
        EXPECT_EQ(z2.d, 3.14);
        EXPECT_EQ(w2.s, "hello");
        // auto& table = storage.tables.get_mut(table_id_before).value().get();
        // EXPECT_TRUE(table.size() == 0);  // moved out
    }
}