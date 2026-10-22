#include <gtest/gtest.h>

#include <epix/shader.hpp>

#include <algorithm>
#include <array>
#include <string>

namespace {
using namespace epix::shader;

class ShaderDefaultContract : public testing::Test {
   protected:
    int loads = 0;
    ShaderCache cache{
        wgpu::Device{},
        [this](const wgpu::Device&, const ShaderCacheSource& source,
               ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
            EXPECT_TRUE(std::holds_alternative<ShaderCacheSource::SpirV>(source.data));
            ++loads;
            return wgpu::ShaderModule{};
        }};
    const epix::assets::AssetId<Shader> root{uuids::uuid::from_string("00000000-0000-0000-0000-00000defa011").value()};
    const epix::assets::AssetId<Shader> leaf{uuids::uuid::from_string("00000000-0000-0000-0000-00000defa012").value()};
    const epix::assets::AssetId<Shader> bridge{uuids::uuid::from_string("00000000-0000-0000-0000-00000defa013").value()};

    static Shader library(unsigned value) {
        auto result = Shader::from_slang(R"slang(
module defaults.leaf;
public float leaf_value() { return 1.0; }
)slang", "defaults/leaf.slang");
        result.shader_defs = {ShaderDefVal::from_uint("LIBRARY_SETTING", value)};
        return result;
    }

    void set_root(std::string_view imports, std::string_view probe = {}, std::string_view value = "0.0") {
        cache.set_shader(root, Shader::from_slang(std::string(imports) + std::string(probe) + R"slang(
[shader("vertex")]
float4 vertex() : SV_Position { return float4()slang" + std::string(value) + R"slang(, 0.0, 0.5, 1.0); }
)slang", "defaults/root.slang"));
    }
};

TEST_F(ShaderDefaultContract, ImportedSettingsReachRootAndSiblingModules) {
    cache.set_shader(leaf, library(13));
    auto sibling = Shader::from_slang(R"slang(
module defaults.sibling;
#if LIBRARY_SETTING != 13
#error missing_setting_in_sibling
#endif
public float sibling_value() { return 2.0; }
)slang", "defaults/sibling.slang");
    sibling.shader_defs = {ShaderDefVal::from_int("SIBLING_SETTING", -7)};
    cache.set_shader(bridge, std::move(sibling));
    set_root("import defaults.leaf;\nimport defaults.sibling;\n", R"slang(
#ifndef LIBRARY_SETTING
#error missing_imported_settings_in_root
#endif
#ifndef SIBLING_SETTING
#error missing_sibling_setting_in_root
#endif
static float check_values[(LIBRARY_SETTING == 13 && SIBLING_SETTING == -7) ? 1 : -1];
)slang", "leaf_value() + sibling_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, TransitiveImportedSettingsReachRoot) {
    cache.set_shader(leaf, library(13));
    cache.set_shader(bridge, Shader::from_slang(R"slang(
module defaults.bridge;
import defaults.leaf;
public float bridge_value() { return leaf_value(); }
)slang", "defaults/bridge.slang"));
    set_root("import defaults.bridge;\n", R"slang(
#ifndef LIBRARY_SETTING
#error missing_transitive_setting
#endif
static float check_value[(LIBRARY_SETTING == 13) ? 1 : -1];
)slang", "bridge_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, UnimportedLibrarySettingsDoNotLeak) {
    cache.set_shader(leaf, library(13));
    set_root({}, "#ifdef LIBRARY_SETTING\n#error leaked_unimported_setting\n#endif\n");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, ConflictingDescriptorAndImportedSettingsAreRejected) {
    cache.set_shader(leaf, library(13));
    set_root("import defaults.leaf;\n", {}, "leaf_value()");
    const std::array defs{ShaderDefVal::from_uint("LIBRARY_SETTING", 19)};
    const auto result = cache.get(CachedPipelineId{1}, root, defs);
    EXPECT_FALSE(result) << "Bevy rejects inconsistent imported ShaderSettings, not silently overriding them";
    if (!result) {
        EXPECT_FALSE(result.error().is_recoverable());
        EXPECT_NE(result.error().message().find("LIBRARY_SETTING"), std::string::npos);
    }
    EXPECT_EQ(loads, 0);
}

TEST_F(ShaderDefaultContract, ConsistentDescriptorAndImportedSettingsAreAccepted) {
    cache.set_shader(leaf, library(13));
    set_root("import defaults.leaf;\n", {}, "leaf_value()");
    const std::array defs{ShaderDefVal::from_uint("LIBRARY_SETTING", 13)};
    const auto result = cache.get(CachedPipelineId{1}, root, defs);
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, ModifiedLibrarySettingsInvalidateRootAndTransitiveModules) {
    cache.set_shader(leaf, library(13));
    cache.set_shader(bridge, Shader::from_slang(R"slang(
module defaults.bridge;
import defaults.leaf;
public float bridge_value() { return leaf_value(); }
)slang", "defaults/bridge.slang"));
    set_root("import defaults.bridge;\n", R"slang(
#ifdef LIBRARY_SETTING
static float expected_setting[(LIBRARY_SETTING == 13) ? 1 : -1];
#endif
)slang", "bridge_value()");
    const CachedPipelineId pipeline{1};
    const auto first = cache.get(pipeline, root, {});
    ASSERT_TRUE(first) << first.error().message();
    const auto affected = cache.set_shader(leaf, library(19));
    EXPECT_NE(std::ranges::find(affected, pipeline), affected.end());
    const auto second = cache.get(pipeline, root, {});
    EXPECT_FALSE(second) << "the root must be compiled again with the changed imported setting";
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, InactiveImportsDoNotContributeSettings) {
    cache.set_shader(leaf, library(13));
    set_root("#if defined(DISABLED) && DISABLED != 0\nimport defaults.leaf;\n#endif\n");
    const std::array defs{ShaderDefVal::from_uint("LIBRARY_SETTING", 19)};
    const auto result = cache.get(CachedPipelineId{1}, root, defs);
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, ConflictingSourceDefineAndImportedSettingAreRejected) {
    cache.set_shader(leaf, library(13));
    set_root("import defaults.leaf;\n", "#define LIBRARY_SETTING 19\n", "leaf_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    EXPECT_FALSE(result);
    if (!result) {
        EXPECT_FALSE(result.error().is_recoverable());
        EXPECT_NE(result.error().message().find("LIBRARY_SETTING"), std::string::npos);
    }
    EXPECT_EQ(loads, 0);
}

TEST_F(ShaderDefaultContract, ConflictingSiblingLibrarySettingsAreRejected) {
    cache.set_shader(leaf, library(13));
    auto sibling = Shader::from_slang(R"slang(
module defaults.sibling;
public float sibling_value() { return 2.0; }
)slang", "defaults/sibling.slang");
    sibling.shader_defs = {ShaderDefVal::from_uint("LIBRARY_SETTING", 19)};
    cache.set_shader(bridge, std::move(sibling));
    set_root("import defaults.leaf;\nimport defaults.sibling;\n", {}, "leaf_value() + sibling_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    EXPECT_FALSE(result);
    if (!result) {
        EXPECT_FALSE(result.error().is_recoverable());
        EXPECT_NE(result.error().message().find("LIBRARY_SETTING"), std::string::npos);
    }
    EXPECT_EQ(loads, 0);
}

TEST_F(ShaderDefaultContract, ModifiedLeafSourceInvalidatesTransitiveSerializedModules) {
    cache.set_shader(leaf, library(13));
    cache.set_shader(bridge, Shader::from_slang(R"slang(
module defaults.bridge;
import defaults.leaf;
public float bridge_value() { return leaf_value(); }
)slang", "defaults/bridge.slang"));
    set_root("import defaults.bridge;\n", {}, "bridge_value()");
    const CachedPipelineId pipeline{1};
    const auto first = cache.get(pipeline, root, {});
    ASSERT_TRUE(first) << first.error().message();
    auto changed = library(13);
    changed.source = Source::slang("module defaults.leaf;\npublic float leaf_value( { return 1.0; }\n");
    const auto affected = cache.set_shader(leaf, std::move(changed));
    EXPECT_NE(std::ranges::find(affected, pipeline), affected.end());
    const auto second = cache.get(pipeline, root, {});
    EXPECT_FALSE(second) << "serialized parent IR cannot conceal an invalid changed dependency";
    if (!second) EXPECT_FALSE(second.error().is_recoverable());
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, CustomModuleNamesIgnoreTokenWhitespace) {
    cache.set_shader(leaf, library(13));
    set_root("import defaults . leaf;\n", {}, "leaf_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, MultipleImportsOnOneLineRemainDistinctModules) {
    cache.set_shader(leaf, library(13));
    cache.set_shader(bridge, Shader::from_slang(
        "module defaults.sibling; public float sibling_value() { return 2.0; }", "defaults/sibling.slang"));
    set_root("import defaults.leaf; import defaults.sibling;\n", {}, "leaf_value() + sibling_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST(ShaderDirectiveMetadata, CommentsStringsAndQuotedSemicolonsDoNotCreateFalseImports) {
    const auto source = Shader::from_slang(R"slang(
/* import ignored.block; */ module actual . library;
// import ignored.line;
#define FAKE_IMPORT import ignored.macro;
static const char* ignored = "import ignored.string;";
import "quoted folder/with;semicolon.slang"; import actual . sibling;
)slang", "directives/root.slang");
    EXPECT_EQ(source.import_path, ShaderImport::custom("actual/library"));
    ASSERT_EQ(source.imports.size(), 2u);
    EXPECT_EQ(source.imports[0], ShaderImport::asset_path(epix::assets::AssetPath{"directives/quoted folder/with;semicolon.slang"}));
    EXPECT_EQ(source.imports[1], ShaderImport::custom("actual/sibling"));
}

TEST_F(ShaderDefaultContract, FalseBooleanLibrarySettingRemainsDefinedAndFalse) {
    auto asset = library(13);
    asset.shader_defs = {ShaderDefVal::from_bool("LIBRARY_FLAG", false)};
    cache.set_shader(leaf, std::move(asset));
    set_root("import defaults.leaf;\n", R"slang(
#ifndef LIBRARY_FLAG
#error missing_false_library_flag
#endif
static float check_false[LIBRARY_FLAG ? -1 : 1];
)slang", "leaf_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, SourceBooleanConflictIsRejected) {
    auto asset = library(13);
    asset.shader_defs = {ShaderDefVal::from_bool("LIBRARY_FLAG", false)};
    cache.set_shader(leaf, std::move(asset));
    set_root("import defaults.leaf;\n", "#define LIBRARY_FLAG true\n", "leaf_value()");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    EXPECT_FALSE(result);
    if (!result) {
        EXPECT_FALSE(result.error().is_recoverable());
        EXPECT_NE(result.error().message().find("LIBRARY_FLAG"), std::string::npos);
    }
    EXPECT_EQ(loads, 0);
}

TEST_F(ShaderDefaultContract, UnrelatedInvalidMacroNamesDoNotAffectRootCompilation) {
    auto asset = library(13);
    asset.shader_defs = {ShaderDefVal::from_uint("NOT A MACRO NAME", 1)};
    cache.set_shader(leaf, std::move(asset));
    set_root({});
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}

TEST_F(ShaderDefaultContract, DisabledInvalidMacroNamesDoNotAffectRootCompilation) {
    auto asset = library(13);
    asset.shader_defs = {ShaderDefVal::from_uint("NOT A MACRO NAME", 1)};
    cache.set_shader(leaf, std::move(asset));
    set_root("#ifdef DISABLED\nimport defaults.leaf;\n#endif\n");
    const auto result = cache.get(CachedPipelineId{1}, root, {});
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(loads, 1);
}
}  // namespace
