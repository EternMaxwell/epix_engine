// Tests for basic shader module types:
//   ShaderId, ShaderDefVal, ValidateShader, Source, ShaderImport, ShaderRef, ShaderLoaderError

#include <gtest/gtest.h>

import std;
import epix.shader;

using namespace epix::shader;

// ===========================================================================
// ShaderId
// ===========================================================================

TEST(ShaderId, NextProducesMonotonicallyIncreasingValues) {
    auto a = ShaderId::next();
    auto b = ShaderId::next();
    EXPECT_LT(a.get(), b.get());
}

TEST(ShaderId, NextProducesDistinctValues) {
    auto a = ShaderId::next();
    auto b = ShaderId::next();
    EXPECT_NE(a, b);
}

TEST(ShaderId, OrderingIsConsistentWithGet) {
    auto a = ShaderId::next();
    auto b = ShaderId::next();
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_FALSE(a > b);
}

TEST(ShaderId, EqualityHoldsForSameValue) {
    auto a = ShaderId::next();
    // Can't create two ShaderId with the same value via public API, but equality must be reflexive
    EXPECT_EQ(a, a);
}

// ===========================================================================
// ShaderDefVal
// ===========================================================================

TEST(ShaderDefVal, ExplicitCtorSetsBoolTrue) {
    ShaderDefVal d("FLAG");
    EXPECT_EQ(d.name, "FLAG");
    EXPECT_EQ(d.value_as_string(), "true");
    EXPECT_TRUE(std::holds_alternative<bool>(d.value));
    EXPECT_TRUE(std::get<bool>(d.value));
}

TEST(ShaderDefVal, FromBoolTrue) {
    auto d = ShaderDefVal::from_bool("A", true);
    EXPECT_EQ(d.name, "A");
    EXPECT_EQ(d.value_as_string(), "true");
}

TEST(ShaderDefVal, FromBoolFalse) {
    auto d = ShaderDefVal::from_bool("A", false);
    EXPECT_EQ(d.value_as_string(), "false");
    EXPECT_FALSE(std::get<bool>(d.value));
}

TEST(ShaderDefVal, FromBoolDefaultIsTrue) {
    auto d = ShaderDefVal::from_bool("X");
    EXPECT_EQ(d.value_as_string(), "true");
}

TEST(ShaderDefVal, FromInt) {
    auto d = ShaderDefVal::from_int("COUNT", 42);
    EXPECT_EQ(d.name, "COUNT");
    EXPECT_EQ(d.value_as_string(), "42");
    EXPECT_TRUE(std::holds_alternative<std::int32_t>(d.value));
    EXPECT_EQ(std::get<std::int32_t>(d.value), 42);
}

TEST(ShaderDefVal, FromIntNegative) {
    auto d = ShaderDefVal::from_int("NEG", -7);
    EXPECT_EQ(d.value_as_string(), "-7");
}

TEST(ShaderDefVal, FromUint) {
    auto d = ShaderDefVal::from_uint("MAX", 4294967295u);
    EXPECT_EQ(d.name, "MAX");
    EXPECT_TRUE(std::holds_alternative<std::uint32_t>(d.value));
    EXPECT_EQ(std::get<std::uint32_t>(d.value), 4294967295u);
    EXPECT_EQ(d.value_as_string(), "4294967295");
}

TEST(ShaderDefVal, EqualityByNameAndValue) {
    auto a = ShaderDefVal::from_bool("X", true);
    auto b = ShaderDefVal::from_bool("X", true);
    auto c = ShaderDefVal::from_bool("X", false);
    auto d = ShaderDefVal::from_bool("Y", true);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(ShaderDefVal, EqualityAcrossTypes) {
    auto a = ShaderDefVal::from_bool("X", true);
    auto b = ShaderDefVal::from_int("X", 1);
    EXPECT_NE(a, b);
}

// ===========================================================================
// ValidateShader
// ===========================================================================

TEST(ValidateShader, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(ValidateShader::Disabled), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(ValidateShader::Enabled), 1);
}

TEST(ValidateShader, DistinctValues) { EXPECT_NE(ValidateShader::Disabled, ValidateShader::Enabled); }

// ===========================================================================
// Source
// ===========================================================================

TEST(Source, WgslFactoryProducesWgsl) {
    auto s = Source::wgsl("fn main() {}");
    EXPECT_TRUE(s.is_wgsl());
    EXPECT_FALSE(s.is_spirv());
    EXPECT_EQ(s.as_str(), "fn main() {}");
}

TEST(Source, WgslEmptyString) {
    auto s = Source::wgsl("");
    EXPECT_TRUE(s.is_wgsl());
    EXPECT_EQ(s.as_str(), "");
}

TEST(Source, SpirVFactoryProducesSpirV) {
    std::vector<std::uint8_t> bytes = {0x03, 0x02, 0x23, 0x07};
    auto s                          = Source::spirv(bytes);
    EXPECT_FALSE(s.is_wgsl());
    EXPECT_TRUE(s.is_spirv());
    const auto& spv = std::get<Source::SpirV>(s.data);
    EXPECT_EQ(spv.bytes, bytes);
}

TEST(Source, SpirVEmptyBytes) {
    auto s = Source::spirv({});
    EXPECT_TRUE(s.is_spirv());
    const auto& spv = std::get<Source::SpirV>(s.data);
    EXPECT_TRUE(spv.bytes.empty());
}

TEST(Source, WgslHoldsCorrectVariant) {
    auto s = Source::wgsl("code");
    EXPECT_TRUE(std::holds_alternative<Source::Wgsl>(s.data));
    EXPECT_FALSE(std::holds_alternative<Source::SpirV>(s.data));
}

TEST(Source, SpirVHoldsCorrectVariant) {
    auto s = Source::spirv({0x01});
    EXPECT_TRUE(std::holds_alternative<Source::SpirV>(s.data));
    EXPECT_FALSE(std::holds_alternative<Source::Wgsl>(s.data));
}

// ===========================================================================
// ShaderImport
// ===========================================================================

TEST(ShaderImport, AssetPathModuleName) {
    auto imp = ShaderImport::asset_path("shaders/common.wgsl");
    EXPECT_EQ(imp.kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(imp.path, "shaders/common.wgsl");
    EXPECT_EQ(imp.module_name(), "\"shaders/common.wgsl\"");
}

TEST(ShaderImport, CustomModuleName) {
    auto imp = ShaderImport::custom("my::module::utils");
    EXPECT_EQ(imp.kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(imp.path, "my::module::utils");
    EXPECT_EQ(imp.module_name(), "my::module::utils");
}

TEST(ShaderImport, AssetPathModuleNameWrapsWithQuotes) {
    auto imp = ShaderImport::asset_path("a/b.wgsl");
    EXPECT_TRUE(imp.module_name().starts_with('"'));
    EXPECT_TRUE(imp.module_name().ends_with('"'));
}

TEST(ShaderImport, EqualityBySameKindAndPath) {
    auto a = ShaderImport::asset_path("x.wgsl");
    auto b = ShaderImport::asset_path("x.wgsl");
    auto c = ShaderImport::asset_path("y.wgsl");
    auto d = ShaderImport::custom("x.wgsl");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);  // same path but different kind
}

TEST(ShaderImport, EmptyAssetPath) {
    auto imp = ShaderImport::asset_path("");
    EXPECT_EQ(imp.module_name(), "\"\"");
}

// ===========================================================================
// ShaderRef
// ===========================================================================

TEST(ShaderRef, DefaultCtorIsDefault) {
    ShaderRef ref;
    EXPECT_TRUE(ref.is_default());
    EXPECT_FALSE(ref.is_handle());
    EXPECT_FALSE(ref.is_path());
    EXPECT_TRUE(std::holds_alternative<ShaderRef::Default>(ref.value));
}

TEST(ShaderRef, FromPathCreatesPath) {
    auto ref = ShaderRef::from_path(std::filesystem::path{"shaders/test.wgsl"});
    EXPECT_FALSE(ref.is_default());
    EXPECT_FALSE(ref.is_handle());
    EXPECT_TRUE(ref.is_path());
    const auto& bp = std::get<ShaderRef::ByPath>(ref.value);
    EXPECT_EQ(bp.path, std::filesystem::path{"shaders/test.wgsl"});
}

TEST(ShaderRef, FromStrCreatesPath) {
    auto ref = ShaderRef::from_str("shaders/test.wgsl");
    EXPECT_TRUE(ref.is_path());
    const auto& bp = std::get<ShaderRef::ByPath>(ref.value);
    // path should be constructible from the string
    EXPECT_FALSE(bp.path.empty());
}

TEST(ShaderRef, ByPathConstructorDirect) {
    ShaderRef::ByPath bp{std::filesystem::path{"a/b.wgsl"}};
    ShaderRef ref(bp);
    EXPECT_TRUE(ref.is_path());
}

// ===========================================================================
// ShaderLoaderError
// ===========================================================================

TEST(ShaderLoaderError, IoFactory) {
    std::error_code ec = std::make_error_code(std::errc::no_such_file_or_directory);
    auto err           = ShaderLoaderError::io(ec, "shaders/missing.wgsl");
    EXPECT_TRUE(std::holds_alternative<ShaderLoaderError::Io>(err.data));
    const auto& io = std::get<ShaderLoaderError::Io>(err.data);
    EXPECT_EQ(io.code, ec);
    EXPECT_EQ(io.path, std::filesystem::path{"shaders/missing.wgsl"});
}

TEST(ShaderLoaderError, ParseFactory) {
    auto err = ShaderLoaderError::parse("shaders/bad.wgsl", 42);
    EXPECT_TRUE(std::holds_alternative<ShaderLoaderError::Parse>(err.data));
    const auto& parse = std::get<ShaderLoaderError::Parse>(err.data);
    EXPECT_EQ(parse.path, std::filesystem::path{"shaders/bad.wgsl"});
    EXPECT_EQ(parse.byte_offset, 42u);
}

TEST(ShaderLoaderError, ParseFactoryDefaultOffset) {
    auto err          = ShaderLoaderError::parse("x.wgsl");
    const auto& parse = std::get<ShaderLoaderError::Parse>(err.data);
    EXPECT_EQ(parse.byte_offset, 0u);
}

TEST(ShaderLoaderError, ToExceptionPtrIo) {
    std::error_code ec = std::make_error_code(std::errc::permission_denied);
    auto err           = ShaderLoaderError::io(ec, "x.wgsl");
    auto ep            = to_exception_ptr(err);
    EXPECT_NE(ep, nullptr);
    // Should be a runtime_error
    try {
        std::rethrow_exception(ep);
        FAIL() << "Expected exception not thrown";
    } catch (const std::runtime_error& ex) {
        std::string msg = ex.what();
        EXPECT_NE(msg.find("ShaderLoaderError::Io"), std::string::npos);
        EXPECT_NE(msg.find("x.wgsl"), std::string::npos);
    }
}

TEST(ShaderLoaderError, ToExceptionPtrParse) {
    auto err = ShaderLoaderError::parse("y.wgsl", 99);
    auto ep  = to_exception_ptr(err);
    EXPECT_NE(ep, nullptr);
    try {
        std::rethrow_exception(ep);
        FAIL() << "Expected exception not thrown";
    } catch (const std::runtime_error& ex) {
        std::string msg = ex.what();
        EXPECT_NE(msg.find("ShaderLoaderError::Parse"), std::string::npos);
        EXPECT_NE(msg.find("y.wgsl"), std::string::npos);
        EXPECT_NE(msg.find("99"), std::string::npos);
    }
}
