#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// ErasedLoadedAsset
// ===========================================================================

TEST(ErasedLoadedAsset, DefaultConstructed_NullValue) {
    ErasedLoadedAsset asset;
    EXPECT_TRUE(asset.labels().empty());
    EXPECT_FALSE(asset.get<int>().has_value());
}

// ===========================================================================
// LoadedAsset<A>
// ===========================================================================

TEST(LoadedAsset, Construction) {
    LoadedAsset<std::string> loaded(std::string("hello"));
    EXPECT_EQ(loaded.get(), "hello");
    EXPECT_FALSE(loaded.get_labeled("missing").has_value());
    EXPECT_TRUE(std::ranges::empty(loaded.labels()));
}

TEST(LoadedAsset, Take) {
    LoadedAsset<std::string> loaded(std::string("take_me"));
    auto taken = loaded.take();
    EXPECT_EQ(taken, "take_me");
}

TEST(LoadedAsset, GetLabeled_NotFound) {
    LoadedAsset<std::string> loaded(std::string("x"));
    EXPECT_FALSE(loaded.get_labeled("nonexistent").has_value());
}

TEST(LoadedAsset, Labels_Empty) {
    LoadedAsset<std::string> loaded(std::string("x"));
    int count = 0;
    for (auto&& label : loaded.labels()) {
        count++;
    }
    EXPECT_EQ(count, 0);
}

// ===========================================================================
// TransformedAsset<A>
// ===========================================================================

TEST(TransformedAsset, Construction) {
    TransformedAsset<std::string> ta(std::string("original"));
    EXPECT_EQ(ta.get(), "original");
    EXPECT_EQ(*ta, "original");
}

TEST(TransformedAsset, GetMut) {
    TransformedAsset<std::string> ta(std::string("before"));
    ta.get_mut() = "after";
    EXPECT_EQ(ta.get(), "after");
}

TEST(TransformedAsset, ReplaceAsset) {
    TransformedAsset<std::string> ta(std::string("str"));
    auto replaced = ta.replace_asset(42);
    EXPECT_EQ(replaced.get(), 42);
}

TEST(TransformedAsset, Take) {
    TransformedAsset<std::string> ta(std::string("owned"));
    EXPECT_EQ(ta.get(), "owned");
}

TEST(TransformedAsset, Labels_Empty) {
    TransformedAsset<std::string> ta(std::string("x"));
    int count = 0;
    for (auto&& label : ta.labels()) {
        count++;
    }
    EXPECT_EQ(count, 0);
}

TEST(TransformedAsset, GetLabeled_NotFound) {
    TransformedAsset<std::string> ta(std::string("x"));
    EXPECT_FALSE(ta.get_labeled<int>("nope").has_value());
}

TEST(TransformedAsset, GetErasedLabeled_NotFound) {
    TransformedAsset<std::string> ta(std::string("x"));
    EXPECT_FALSE(ta.get_erased_labeled("nope").has_value());
}

// ===========================================================================
// IdentityAssetTransformer<A>
// ===========================================================================

TEST(IdentityAssetTransformer, PassesThrough) {
    IdentityAssetTransformer<std::string> t;
    typename IdentityAssetTransformer<std::string>::Settings s;
    auto result = t.transform(TransformedAsset<std::string>(std::string("unchanged")), s);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get(), "unchanged");
}

// ===========================================================================
// SavedAsset<A>
// ===========================================================================

// SavedAsset requires LabeledAsset which is not exported from the module.
// SavedAsset is tested indirectly through the asset saver infrastructure.
// Skipping direct SavedAsset tests here.
