module;

#include <gtest/gtest.h>

export module epix.core:tests.bundle_info;

import std;
import :bundle;
import :world;

using namespace core;

namespace {
struct X {
    int v;
    X(int vv) : v(vv) {}
};
struct Z {
    double d;
    Z(double dd) : d(dd) {}
};
}  // namespace
template <>
struct core::sparse_component<Z> : std::true_type {};

TEST(core, bundle_info) {
    auto registry = std::make_shared<TypeRegistry>();
    World world(WorldId(1), registry);

    // Do NOT pre-register component infos; register_info will add them automatically.
    using MyBundle = InitializeBundle<std::tuple<X, Z>, std::tuple<std::tuple<int>, std::tuple<double>>>;
    auto& bundles  = world.bundles_mut();
    BundleId bid = bundles.register_info<MyBundle>(world.type_registry(), world.components_mut(), world.storage_mut());
    auto& bundle_info = bundles.get(bid).value().get();

    TypeId tx = registry->type_id<X>();
    TypeId tz = registry->type_id<Z>();

    // insert bundle into empty archetype (id 0)
    ArchetypeId base = ArchetypeId(0);

    // before insert, base archetype should not have a cached insert edge for this bundle
    {
        auto before = world.archetypes().get(base).value().get().edges().get_archetype_after_bundle_insert(bid);
        EXPECT_TRUE(!before.has_value());
    }

    ArchetypeId new_id =
        bundle_info.insert_bundle_into_archetype(world.archetypes_mut(), world.storage_mut(), world.components(), base);
    EXPECT_TRUE(new_id.get() != 0);

    // the base archetype should have cached the insert edge
    {
        auto cached = world.archetypes().get(base).value().get().edges().get_archetype_after_bundle_insert(bid);
        EXPECT_TRUE(cached.has_value());
        EXPECT_TRUE(cached.value() == new_id);
        auto detail_opt =
            world.archetypes().get(base).value().get().edges().get_archetype_after_bundle_insert_detail(bid);
        EXPECT_TRUE(detail_opt.has_value());
        auto& detail = detail_opt.value().get();
        // inserted components should include X and Z
        bool found_tx = std::ranges::any_of(detail.inserted(), [&](TypeId id) { return id == tx; });
        bool found_tz = std::ranges::any_of(detail.inserted(), [&](TypeId id) { return id == tz; });
        EXPECT_TRUE(found_tx && found_tz);
        // all statuses should be Added when inserting into empty archetype
        for (auto s : detail.iter_status()) {
            EXPECT_TRUE(s == ComponentStatus::Added);
        }
    }

    auto& new_arch = world.archetypes().get(new_id).value().get();
    // check that archetype contains both components in expected storages
    EXPECT_TRUE(new_arch.contains(tx));
    EXPECT_TRUE(new_arch.contains(tz));
    bool tx_in_table  = std::ranges::any_of(new_arch.table_components(), [&](TypeId id) { return id == tx; });
    bool tz_in_sparse = std::ranges::any_of(new_arch.sparse_components(), [&](TypeId id) { return id == tz; });
    EXPECT_TRUE(tx_in_table);
    EXPECT_TRUE(tz_in_sparse);

    // calling insert again should hit cached edge and return same id
    ArchetypeId new_id2 =
        bundle_info.insert_bundle_into_archetype(world.archetypes_mut(), world.storage_mut(), world.components(), base);
    EXPECT_TRUE(new_id == new_id2);

    // remove the bundle from the new archetype; should return the base archetype id and cache the removal on
    // new_archetype
    auto opt = bundle_info.remove_bundle_from_archetype(world.archetypes_mut(), world.storage_mut(), world.components(),
                                                        new_id, true);
    EXPECT_TRUE(opt.has_value());
    EXPECT_TRUE(opt.value() == base);
    {
        auto rem_cached = world.archetypes().get(new_id).value().get().edges().get_archetype_after_bundle_remove(bid);
        EXPECT_TRUE(rem_cached.has_value());
        EXPECT_TRUE(rem_cached.value().has_value());
        EXPECT_TRUE(rem_cached.value().value() == base);
    }

    // trying to remove from empty archetype without ignoring missing should cache a 'take' with nullopt
    auto opt2 = bundle_info.remove_bundle_from_archetype(world.archetypes_mut(), world.storage_mut(),
                                                         world.components(), base, false);
    EXPECT_TRUE(!opt2.has_value());
    {
        auto take_cached = world.archetypes().get(base).value().get().edges().get_archetype_after_bundle_take(bid);
        EXPECT_TRUE(take_cached.has_value());
        EXPECT_TRUE(!take_cached.value().has_value());
    }
}
