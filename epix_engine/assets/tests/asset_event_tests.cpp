#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace epix::assets;

// ===========================================================================
// AssetEvent<T> — construction and predicates
// ===========================================================================

TEST(AssetEvent, Added) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::added(id);
    EXPECT_TRUE(ev.is_added());
    EXPECT_FALSE(ev.is_removed());
    EXPECT_FALSE(ev.is_modified());
    EXPECT_FALSE(ev.is_unused());
    EXPECT_FALSE(ev.is_loaded_with_dependencies());
    EXPECT_EQ(ev.id, id);
}

TEST(AssetEvent, Removed) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::removed(id);
    EXPECT_TRUE(ev.is_removed());
    EXPECT_FALSE(ev.is_added());
}

TEST(AssetEvent, Modified) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::modified(id);
    EXPECT_TRUE(ev.is_modified());
}

TEST(AssetEvent, Unused) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::unused(id);
    EXPECT_TRUE(ev.is_unused());
}

TEST(AssetEvent, LoadedWithDependencies) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::loaded_with_dependencies(id);
    EXPECT_TRUE(ev.is_loaded_with_dependencies());
}

// ===========================================================================
// AssetEvent<T> — predicates with specific id
// ===========================================================================

TEST(AssetEvent, IsAddedWithId_Match) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::added(id);
    EXPECT_TRUE(ev.is_added(id));
}

TEST(AssetEvent, IsAddedWithId_NoMatch) {
    auto id1 = AssetId<std::string>::invalid();
    // Create a different id by using an Assets collection
    Assets<std::string> assets;
    auto h2  = assets.emplace("x");
    auto id2 = h2.id();
    auto ev  = AssetEvent<std::string>::added(id1);
    EXPECT_FALSE(ev.is_added(id2));
}

TEST(AssetEvent, IsRemovedWithId) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::removed(id);
    EXPECT_TRUE(ev.is_removed(id));
}

TEST(AssetEvent, IsModifiedWithId) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::modified(id);
    EXPECT_TRUE(ev.is_modified(id));
}

TEST(AssetEvent, IsUnusedWithId) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::unused(id);
    EXPECT_TRUE(ev.is_unused(id));
}

TEST(AssetEvent, IsLoadedWithDependenciesWithId) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::loaded_with_dependencies(id);
    EXPECT_TRUE(ev.is_loaded_with_dependencies(id));
}

// ===========================================================================
// AssetEvent<T> — wrong event type with id
// ===========================================================================

TEST(AssetEvent, IsRemoved_ButActuallyAdded) {
    auto id = AssetId<std::string>::invalid();
    auto ev = AssetEvent<std::string>::added(id);
    EXPECT_FALSE(ev.is_removed(id));
    EXPECT_FALSE(ev.is_modified(id));
    EXPECT_FALSE(ev.is_unused(id));
    EXPECT_FALSE(ev.is_loaded_with_dependencies(id));
}
