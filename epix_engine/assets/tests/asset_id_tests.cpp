#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace epix::assets;

// ===========================================================================
// AssetIndex — basic properties
// ===========================================================================

// AssetIndex constructor is protected so we test via AssetIndexAllocator.

// ===========================================================================
// AssetId<T> — construction, queries, comparison
// ===========================================================================

TEST(AssetId, Invalid_IsUuid) {
    auto id = AssetId<std::string>::invalid();
    EXPECT_TRUE(id.is_uuid());
    EXPECT_FALSE(id.is_index());
}

TEST(AssetId, Invalid_NotEqualToAnotherInvalid_OfDifferentType) {
    // Two invalids of different types are still the same UUID value internally
    auto a = AssetId<std::string>::invalid();
    auto b = AssetId<int>::invalid();
    // UntypedAssetId comparison should differ on type
    UntypedAssetId ua(a);
    UntypedAssetId ub(b);
    EXPECT_NE(ua, ub);
}

TEST(AssetId, SameInvalid_EqualToSelf) {
    auto a = AssetId<std::string>::invalid();
    auto b = AssetId<std::string>::invalid();
    EXPECT_EQ(a, b);
}

TEST(AssetId, ToString_ContainsTypeName) {
    auto id = AssetId<std::string>::invalid();
    auto s  = id.to_string();
    // Should contain "AssetId<" and "UUID("
    EXPECT_NE(s.find("AssetId<"), std::string::npos);
    EXPECT_NE(s.find("UUID("), std::string::npos);
}

TEST(AssetId, ToStringShort_NoTypeName) {
    auto id = AssetId<std::string>::invalid();
    auto s  = id.to_string_short();
    EXPECT_EQ(s.find("AssetId<"), std::string::npos);
    EXPECT_NE(s.find("UUID("), std::string::npos);
}

// ===========================================================================
// UntypedAssetId
// ===========================================================================

TEST(UntypedAssetId, FromTypedId) {
    auto typed = AssetId<std::string>::invalid();
    UntypedAssetId untyped(typed);
    EXPECT_TRUE(untyped.is_uuid());
    EXPECT_FALSE(untyped.is_index());
}

TEST(UntypedAssetId, TryTyped_CorrectType) {
    auto typed = AssetId<std::string>::invalid();
    UntypedAssetId untyped(typed);
    auto result = untyped.try_typed<std::string>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), typed);
}

TEST(UntypedAssetId, TryTyped_WrongType) {
    auto typed = AssetId<std::string>::invalid();
    UntypedAssetId untyped(typed);
    auto result = untyped.try_typed<int>();
    EXPECT_FALSE(result.has_value());
}

TEST(UntypedAssetId, Typed_CorrectType) {
    auto typed = AssetId<std::string>::invalid();
    UntypedAssetId untyped(typed);
    EXPECT_NO_THROW({
        auto t = untyped.typed<std::string>();
        EXPECT_EQ(t, typed);
    });
}

TEST(UntypedAssetId, Typed_WrongType_Throws) {
    auto typed = AssetId<std::string>::invalid();
    UntypedAssetId untyped(typed);
    EXPECT_THROW(untyped.typed<int>(), std::bad_optional_access);
}

TEST(UntypedAssetId, ToString_ContainsTypeName) {
    auto id  = AssetId<std::string>::invalid();
    auto uid = UntypedAssetId(id);
    auto s   = uid.to_string();
    EXPECT_NE(s.find("UntypedAssetId<"), std::string::npos);
}

TEST(UntypedAssetId, Equality_SameType) {
    auto a = AssetId<std::string>::invalid();
    UntypedAssetId ua(a);
    UntypedAssetId ub(a);
    EXPECT_EQ(ua, ub);
}

TEST(UntypedAssetId, Inequality_DifferentType) {
    auto si = AssetId<std::string>::invalid();
    auto ii = AssetId<int>::invalid();
    UntypedAssetId ua(si);
    UntypedAssetId ub(ii);
    EXPECT_NE(ua, ub);
}

TEST(UntypedAssetId, CrossTypeEquality_TypedVsUntyped) {
    auto typed = AssetId<std::string>::invalid();
    UntypedAssetId untyped(typed);
    EXPECT_TRUE(typed == untyped);
    EXPECT_TRUE(untyped == typed);
}

// ===========================================================================
// UntypedAssetId static invalid()
// ===========================================================================

TEST(UntypedAssetId, InvalidFactory) {
    auto inv = UntypedAssetId::invalid<std::string>();
    EXPECT_TRUE(inv.is_uuid());
}

// ===========================================================================
// AssetId hashing
// ===========================================================================

TEST(AssetIdHash, SameIdSameHash) {
    auto a = AssetId<std::string>::invalid();
    auto b = AssetId<std::string>::invalid();
    EXPECT_EQ(std::hash<AssetId<std::string>>{}(a), std::hash<AssetId<std::string>>{}(b));
}

TEST(UntypedAssetIdHash, SameIdSameHash) {
    auto a = AssetId<std::string>::invalid();
    UntypedAssetId ua(a);
    UntypedAssetId ub(a);
    EXPECT_EQ(std::hash<UntypedAssetId>{}(ua), std::hash<UntypedAssetId>{}(ub));
}

TEST(UntypedAssetIdHash, DifferentType_DifferentHash) {
    auto si = AssetId<std::string>::invalid();
    auto ii = AssetId<int>::invalid();
    UntypedAssetId ua(si);
    UntypedAssetId ub(ii);
    EXPECT_NE(std::hash<UntypedAssetId>{}(ua), std::hash<UntypedAssetId>{}(ub));
}
