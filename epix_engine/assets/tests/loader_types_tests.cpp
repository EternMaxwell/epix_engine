#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace epix::assets;

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
// AssetLoader concept
// ===========================================================================

namespace {
struct SimpleLoader {
    using Asset    = std::string;
    using Settings = EmptySettings;
    using Error    = std::string;

    std::span<std::string_view> extensions() const {
        static std::array<std::string_view, 1> exts = {"txt"};
        return exts;
    }
    std::expected<std::string, std::string> load(std::istream& stream, const EmptySettings&, LoadContext& ctx) const {
        return std::string("loaded");
    }
};
}  // namespace

static_assert(AssetLoader<SimpleLoader>);

// ===========================================================================
// ErasedLoadedAsset
// ===========================================================================

TEST(ErasedLoadedAsset, FromAsset_TypeId) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("hello"));
    EXPECT_EQ(erased.asset_type_id(), epix::meta::type_id<std::string>{});
}

TEST(ErasedLoadedAsset, FromAsset_TypeName) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("hello"));
    EXPECT_FALSE(erased.asset_type_name().empty());
}

TEST(ErasedLoadedAsset, Get_CorrectType) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("world"));
    auto maybe  = erased.get<std::string>();
    ASSERT_TRUE(maybe.has_value());
    EXPECT_EQ(maybe->get(), "world");
}

TEST(ErasedLoadedAsset, Get_WrongType_ReturnsNullopt) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("world"));
    auto maybe  = erased.get<int>();
    EXPECT_FALSE(maybe.has_value());
}

TEST(ErasedLoadedAsset, Get_Const_CorrectType) {
    const auto erased = ErasedLoadedAsset::from_asset(std::string("const_world"));
    auto maybe        = erased.get<std::string>();
    ASSERT_TRUE(maybe.has_value());
    EXPECT_EQ(maybe->get(), "const_world");
}

TEST(ErasedLoadedAsset, Get_Const_WrongType_ReturnsNullopt) {
    const auto erased = ErasedLoadedAsset::from_asset(std::string("const_world"));
    auto maybe        = erased.get<int>();
    EXPECT_FALSE(maybe.has_value());
}

TEST(ErasedLoadedAsset, Take_CorrectType) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("take_me"));
    auto taken  = std::move(erased).take<std::string>();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, "take_me");
}

TEST(ErasedLoadedAsset, Take_WrongType_ReturnsNullopt) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("take_me"));
    auto taken  = std::move(erased).take<int>();
    EXPECT_FALSE(taken.has_value());
}

TEST(ErasedLoadedAsset, Labels_Empty) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("x"));
    EXPECT_TRUE(erased.labels().empty());
}

TEST(ErasedLoadedAsset, GetLabeled_NotFound) {
    auto erased = ErasedLoadedAsset::from_asset(std::string("x"));
    EXPECT_FALSE(erased.get_labeled("missing").has_value());
}

TEST(ErasedLoadedAsset, GetLabeledById_NotFound) {
    auto erased  = ErasedLoadedAsset::from_asset(std::string("x"));
    auto fake_id = UntypedAssetId(AssetId<std::string>::invalid());
    EXPECT_FALSE(erased.get_labeled_by_id(fake_id).has_value());
}

// ===========================================================================
// SavedAsset<A>
// ===========================================================================

// SavedAsset requires LabeledAsset which is not exported from the module.
// SavedAsset is tested indirectly through the asset saver infrastructure.
// Skipping direct SavedAsset tests here.
