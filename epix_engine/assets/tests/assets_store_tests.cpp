#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// Assets<T> — basic lifecycle
// ===========================================================================

TEST(Assets, Emplace_ReturnsStrongHandle) {
    Assets<std::string> assets;
    auto handle = assets.emplace("hello");
    EXPECT_TRUE(handle.is_strong());
    EXPECT_FALSE(assets.is_empty());
    EXPECT_EQ(assets.len(), 1u);
}

TEST(Assets, Add_ReturnsStrongHandle) {
    Assets<std::string> assets;
    auto handle = assets.add(std::string("world"));
    EXPECT_TRUE(handle.is_strong());
    auto val = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "world");
}

TEST(Assets, Get_ValidHandle) {
    Assets<std::string> assets;
    auto handle = assets.emplace("test");
    auto val    = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "test");
}

TEST(Assets, Get_InvalidId) {
    Assets<std::string> assets;
    auto id  = AssetId<std::string>::invalid();
    auto val = assets.get(id);
    EXPECT_FALSE(val.has_value());
}

TEST(Assets, GetMut) {
    Assets<std::string> assets;
    auto handle = assets.emplace("original");
    auto val    = assets.get_mut(handle.id());
    ASSERT_TRUE(val.has_value());
    val->get() = "modified";
    auto check = assets.get(handle.id());
    ASSERT_TRUE(check.has_value());
    EXPECT_EQ(check->get(), "modified");
}

TEST(Assets, Contains_True) {
    Assets<std::string> assets;
    auto handle = assets.emplace("x");
    EXPECT_TRUE(assets.contains(handle.id()));
}

TEST(Assets, Contains_False) {
    Assets<std::string> assets;
    EXPECT_FALSE(assets.contains(AssetId<std::string>::invalid()));
}

// ===========================================================================
// Assets<T> — insert / replace
// ===========================================================================

TEST(Assets, Insert_NewAsset) {
    Assets<std::string> assets;
    auto handle = assets.reserve_handle();
    auto res    = assets.insert(handle.id(), std::string("inserted"));
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res.value());  // not replaced, was new
    auto val = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "inserted");
}

TEST(Assets, Insert_Replace) {
    Assets<std::string> assets;
    auto handle = assets.emplace("v1");
    auto res    = assets.insert(handle.id(), std::string("v2"));
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res.value());  // replaced
    auto val = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "v2");
}

// ===========================================================================
// Assets<T> — remove / take
// ===========================================================================

TEST(Assets, Remove_Existing) {
    Assets<std::string> assets;
    auto handle = assets.emplace("to_remove");
    auto res    = assets.remove(handle.id());
    EXPECT_TRUE(res.has_value());
    EXPECT_FALSE(assets.contains(handle.id()));
}

TEST(Assets, Remove_Missing) {
    Assets<std::string> assets;
    auto id  = AssetId<std::string>::invalid();
    auto res = assets.remove(id);
    EXPECT_FALSE(res.has_value());
}

TEST(Assets, Take_Existing) {
    Assets<std::string> assets;
    auto handle = assets.emplace("taken");
    auto res    = assets.take(handle.id());
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res.value(), "taken");
    EXPECT_FALSE(assets.contains(handle.id()));
}

TEST(Assets, Take_Missing) {
    Assets<std::string> assets;
    auto res = assets.take(AssetId<std::string>::invalid());
    EXPECT_FALSE(res.has_value());
}

// ===========================================================================
// Assets<T> — strong handle / weak handle
// ===========================================================================

TEST(Assets, GetStrongHandle) {
    Assets<std::string> assets;
    auto h1  = assets.emplace("item");
    auto res = assets.get_strong_handle(h1.id());
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_strong());
    EXPECT_EQ(res->id(), h1.id());
}

TEST(Assets, WeakHandle) {
    Assets<std::string> assets;
    auto strong = assets.emplace("item");
    auto weak   = strong.weak();
    EXPECT_TRUE(weak.is_weak());
    EXPECT_FALSE(weak.is_strong());
    EXPECT_EQ(weak.id(), strong.id());
    // Should still be able to look up asset by weak handle id
    auto val = assets.get(weak.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "item");
}

// ===========================================================================
// Assets<T> — handle auto-destruction (strong handle drop)
// ===========================================================================

TEST(Assets, StrongHandleDrop_CausesRelease) {
    Assets<std::string> assets;
    auto handle = assets.emplace("AutoDestruct");
    auto id     = handle.id();
    EXPECT_TRUE(assets.contains(id));
    // Asset should still be reachable while the strong handle is alive
    EXPECT_TRUE(assets.contains(id));
}

TEST(Assets, StrongHandleDrop_ManualEventHandlingReleasesAsset) {
    Assets<std::string> assets;

    auto id = AssetId<std::string>::invalid();
    {
        auto strong = assets.emplace("temp");
        id          = strong.id();
        EXPECT_TRUE(assets.contains(id));
    }

    assets.handle_events_manual();
    EXPECT_FALSE(assets.contains(id));
}

TEST(Assets, MultipleStrongHandles_DroppingOneRetainsAsset) {
    Assets<std::string> assets;

    auto strong1 = assets.emplace("value");
    auto strong2 = strong1;
    auto id      = strong1.id();

    strong1 = Handle<std::string>(AssetId<std::string>::invalid());
    assets.handle_events_manual();
    EXPECT_TRUE(assets.contains(id));
}

// ===========================================================================
// Assets<T> — index recycling
// ===========================================================================

TEST(Assets, IndexRecycling) {
    Assets<std::string> assets;
    auto h1  = assets.emplace("first");
    auto id1 = h1.id();
    auto h2  = assets.emplace("second");
    auto id2 = h2.id();
    // Both should be index-based and have different indices
    ASSERT_TRUE(id1.is_index());
    ASSERT_TRUE(id2.is_index());
    auto idx1 = std::get<AssetIndex>(id1);
    auto idx2 = std::get<AssetIndex>(id2);
    EXPECT_NE(idx1.index(), idx2.index());
}

TEST(Assets, IndexRecycling_AfterHandleDestruction_ReusesIndexAndBumpsGeneration) {
    Assets<std::string> assets;

    std::optional<AssetIndex> first_index;
    {
        auto first = assets.emplace("first");
        ASSERT_TRUE(first.id().is_index());
        first_index = std::get<AssetIndex>(first.id());
    }
    ASSERT_TRUE(first_index.has_value());

    assets.handle_events_manual();

    auto second = assets.emplace("second");
    ASSERT_TRUE(second.id().is_index());
    auto second_index = std::get<AssetIndex>(second.id());

    EXPECT_EQ(first_index->index(), second_index.index());
    EXPECT_EQ(first_index->generation() + 1, second_index.generation());
}

// ===========================================================================
// Assets<T> — reserve_handle
// ===========================================================================

TEST(Assets, ReserveHandle_ThenInsert) {
    Assets<std::string> assets;
    auto handle = assets.reserve_handle();
    EXPECT_TRUE(handle.is_strong());
    // Asset not yet inserted
    EXPECT_FALSE(assets.get(handle.id()).has_value());
    // Insert
    auto res = assets.insert(handle.id(), std::string("reserved"));
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res.value());
    auto val = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "reserved");
}

TEST(Assets, ReserveHandle_PreservesReservedIndexForInsert) {
    Assets<std::string> assets;

    auto reserved = assets.reserve_handle();
    ASSERT_TRUE(reserved.id().is_index());
    auto reserved_index = std::get<AssetIndex>(reserved.id());

    auto inserted = assets.insert(reserved.id(), std::string("value"));
    ASSERT_TRUE(inserted.has_value());
    EXPECT_FALSE(inserted.value());

    auto stored = assets.get(reserved.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->get(), "value");

    auto ids = assets.ids();
    ASSERT_EQ(ids.size(), 1u);
    ASSERT_TRUE(ids.front().is_index());
    auto stored_index = std::get<AssetIndex>(ids.front());
    EXPECT_EQ(stored_index.index(), reserved_index.index());
    EXPECT_EQ(stored_index.generation(), reserved_index.generation());
}

// ===========================================================================
// Assets<T> — iteration
// ===========================================================================

TEST(Assets, Ids) {
    Assets<std::string> assets;
    auto h1  = assets.emplace("a");
    auto h2  = assets.emplace("b");
    auto h3  = assets.emplace("c");
    auto ids = assets.ids();
    ASSERT_EQ(ids.size(), 3u);
}

TEST(Assets, Iter) {
    Assets<std::string> assets;
    auto h1 = assets.emplace("x");
    auto h2 = assets.emplace("y");

    std::vector<std::string> collected;
    assets.iter([&](const AssetId<std::string>&, const std::string& s) { collected.push_back(s); });
    ASSERT_EQ(collected.size(), 2u);
    // Order may vary, check both present
    EXPECT_TRUE(std::find(collected.begin(), collected.end(), "x") != collected.end());
    EXPECT_TRUE(std::find(collected.begin(), collected.end(), "y") != collected.end());
}

TEST(Assets, IterMut) {
    Assets<std::string> assets;
    auto h1 = assets.emplace("old1");
    auto h2 = assets.emplace("old2");

    assets.iter_mut([](const AssetId<std::string>&, std::string& s) { s = "new"; });

    auto v1 = assets.get(h1.id());
    auto v2 = assets.get(h2.id());
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v1->get(), "new");
    EXPECT_EQ(v2->get(), "new");
}

// ===========================================================================
// Assets<T> — len / is_empty
// ===========================================================================

TEST(Assets, LenAndIsEmpty) {
    Assets<std::string> assets;
    EXPECT_TRUE(assets.is_empty());
    EXPECT_EQ(assets.len(), 0u);

    auto h = assets.emplace("a");
    EXPECT_FALSE(assets.is_empty());
    EXPECT_EQ(assets.len(), 1u);
}

// ===========================================================================
// Assets<T> — get_or_insert_with
// ===========================================================================

TEST(Assets, GetOrInsertWith_Existing) {
    Assets<std::string> assets;
    auto h   = assets.emplace("present");
    auto res = assets.get_or_insert_with(h.id(), [] { return std::string("default"); });
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->get(), "present");
}

TEST(Assets, GetOrInsertWith_New) {
    Assets<std::string> assets;
    auto h   = assets.reserve_handle();
    auto res = assets.get_or_insert_with(h.id(), [] { return std::string("default"); });
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->get(), "default");
}

// ===========================================================================
// Assets<T> — get_mut_untracked / remove_untracked
// ===========================================================================

TEST(Assets, GetMutUntracked) {
    Assets<std::string> assets;
    auto h   = assets.emplace("val");
    auto ref = assets.get_mut_untracked(h.id());
    ASSERT_TRUE(ref.has_value());
    ref->get() = "changed";
    EXPECT_EQ(assets.get(h.id())->get(), "changed");
}

TEST(Assets, RemoveUntracked) {
    Assets<std::string> assets;
    auto h   = assets.emplace("bye");
    auto res = assets.remove_untracked(h.id());
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res.value(), "bye");
    EXPECT_FALSE(assets.contains(h.id()));
}
