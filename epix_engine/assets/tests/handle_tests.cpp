#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// Handle<T> - construction via weak id
// ===========================================================================

TEST(Handle, WeakFromAssetId) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    EXPECT_TRUE(h.is_weak());
    EXPECT_FALSE(h.is_strong());
    EXPECT_EQ(h.id(), id);
}

TEST(Handle, WeakFromUuid) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    EXPECT_TRUE(h.is_weak());
    EXPECT_TRUE(h.id().is_uuid());
}

// ===========================================================================
// Handle<T> - weak() copies correctly
// ===========================================================================

TEST(Handle, Weak_ProducesWeakCopy) {
    Assets<std::string> assets;
    auto strong = assets.emplace("test");
    EXPECT_TRUE(strong.is_strong());
    auto weak = strong.weak();
    EXPECT_TRUE(weak.is_weak());
    EXPECT_EQ(weak.id(), strong.id());
}

// ===========================================================================
// Handle<T> - equality
// ===========================================================================

TEST(Handle, Equality_WeakSameId) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> a(id);
    Handle<std::string> b(id);
    EXPECT_EQ(a, b);
}

TEST(Handle, Equality_StrongSameAsset) {
    Assets<std::string> assets;
    auto h1 = assets.emplace("x");
    auto h2 = assets.get_strong_handle(h1.id());
    ASSERT_TRUE(h2.has_value());
    // Strong handles from get_strong_handle may be different shared_ptrs,
    // but they should have the same id
    EXPECT_EQ(h1.id(), h2->id());
}

// ===========================================================================
// Handle<T> - path()
// ===========================================================================

TEST(Handle, Path_WeakHasNone) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    EXPECT_FALSE(h.path().has_value());
}

// ===========================================================================
// Handle<T> - untyped conversion via .untyped()
// ===========================================================================

TEST(Handle, Untyped_RoundTrip) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    UntypedHandle uh = h.untyped();
    EXPECT_TRUE(uh.is_weak());
    auto back = uh.try_typed<std::string>();
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->id(), id);
}

TEST(Handle, Untyped_WrongType) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    UntypedHandle uh = h.untyped();
    auto wrong       = uh.try_typed<int>();
    EXPECT_FALSE(wrong.has_value());
}

TEST(Handle, Untyped_TypedThrowsOnMismatch) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    UntypedHandle uh = h.untyped();
    EXPECT_THROW(uh.typed<int>(), std::bad_optional_access);
}

// ===========================================================================
// UntypedHandle
// ===========================================================================

TEST(UntypedHandle, ConstructFromTyped) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    UntypedHandle uh = h.untyped();
    EXPECT_TRUE(uh.is_weak());
    EXPECT_EQ(uh.id(), UntypedAssetId(id));
}

TEST(UntypedHandle, WeakCopy) {
    Assets<std::string> assets;
    auto strong      = assets.emplace("test");
    UntypedHandle uh = strong.untyped();
    EXPECT_TRUE(uh.is_strong());
    auto weak = uh.weak();
    EXPECT_TRUE(weak.is_weak());
    EXPECT_EQ(weak.id(), uh.id());
}

TEST(UntypedHandle, Path_WeakHasNone) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    UntypedHandle uh = h.untyped();
    auto p           = uh.path();
    EXPECT_FALSE(p.has_value());
}

TEST(UntypedHandle, Equality) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h1(id);
    Handle<std::string> h2(id);
    UntypedHandle a = h1.untyped();
    UntypedHandle b = h2.untyped();
    EXPECT_EQ(a, b);
}

// ===========================================================================
// Handle<T> constructed from UntypedHandle
// ===========================================================================

TEST(Handle, FromUntypedHandle_Correct) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> orig(id);
    UntypedHandle uh = orig.untyped();
    Handle<std::string> back(uh);
    EXPECT_EQ(back.id(), id);
}

TEST(Handle, FromUntypedHandle_WrongType_Throws) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> orig(id);
    UntypedHandle uh = orig.untyped();
    EXPECT_THROW({ auto h = uh.typed<int>(); }, std::bad_optional_access);
}

TEST(Handle, MoveFromUntypedHandle_Correct) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> orig(id);
    UntypedHandle uh = orig.untyped();
    Handle<std::string> back(std::move(uh));
    EXPECT_EQ(back.id(), id);
}

TEST(Handle, AssignFromUntypedHandle) {
    auto id1 = AssetId<std::string>::invalid();
    Handle<std::string> h(id1);
    Handle<std::string> other(id1);
    UntypedHandle uh = other.untyped();
    h                = uh;
    EXPECT_TRUE(h.id().is_uuid());
}

TEST(Handle, AssignFromUntypedHandle_WrongType_Throws) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    auto int_id = AssetId<int>::invalid();
    Handle<int> int_h(int_id);
    UntypedHandle uh = int_h.untyped();
    EXPECT_THROW({ h = uh; }, std::runtime_error);
}

// ===========================================================================
// Handle<T> - implicit conversion to AssetId
// ===========================================================================

TEST(Handle, ImplicitConversionToAssetId) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    AssetId<std::string> converted = h;
    EXPECT_EQ(converted, id);
}

// ===========================================================================
// UntypedHandle::meta_transform()
// ===========================================================================

TEST(UntypedHandle, MetaTransform_WeakHandle_ReturnsNull) {
    auto id = AssetId<std::string>::invalid();
    Handle<std::string> h(id);
    UntypedHandle uh = h.untyped();
    EXPECT_EQ(uh.meta_transform(), nullptr);
}

TEST(UntypedHandle, MetaTransform_StrongHandle_WithoutTransform_ReturnsNull) {
    Assets<std::string> assets;
    auto strong      = assets.emplace("test");
    UntypedHandle uh = strong.untyped();
    EXPECT_TRUE(uh.is_strong());
    EXPECT_EQ(uh.meta_transform(), nullptr);
}
