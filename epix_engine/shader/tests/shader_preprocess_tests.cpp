#include <gtest/gtest.h>

import epix.assets;
import epix.shader;

using namespace epix::assets;
using namespace epix::shader;

namespace {

void expect_asset_import(const ShaderImport& import, std::string_view expected_source, std::string_view expected_path) {
    ASSERT_TRUE(import.is_asset_path());
    const auto& path = import.as_asset_path();
    ASSERT_TRUE(path.source.as_str().has_value());
    EXPECT_EQ(path.source.as_str().value(), expected_source);
    EXPECT_EQ(path.path.generic_string(), expected_path);
}

}  // namespace

TEST(ShaderPreprocessWgsl, FallsBackToAssetPathWhenNoDirectivesExist) {
    auto [import_path, imports] = Shader::preprocess("", "shaders/main.wgsl");

    ASSERT_TRUE(import_path.is_asset_path());
    EXPECT_EQ(import_path.as_asset_path(), AssetPath("shaders/main.wgsl"));
    EXPECT_TRUE(imports.empty());
}

TEST(ShaderPreprocessWgsl, ResolvesAllFourImportFormsToDistinctTargets) {
    const char* source =
        "#define_import_path ui::button\n"
        "#import ui::custom\n"
        "#import \"embedded://explicit/shared/full.wgsl\"\n"
        "#import \"/shared/root.wgsl\"\n"
        "#import \"common/relative.wgsl\"\n";

    auto [import_path, imports] = Shader::preprocess(source, "embedded://mesh/main.wgsl");

    ASSERT_TRUE(import_path.is_custom());
    EXPECT_EQ(import_path.as_custom(), "ui::button");
    ASSERT_EQ(imports.size(), 4u);

    ASSERT_TRUE(imports[0].is_custom());
    EXPECT_EQ(imports[0].as_custom(), "ui::custom");

    expect_asset_import(imports[1], "embedded", "explicit/shared/full.wgsl");
    expect_asset_import(imports[2], "embedded", "shared/root.wgsl");
    expect_asset_import(imports[3], "embedded", "mesh/common/relative.wgsl");

    EXPECT_NE(imports[1].as_asset_path().path.generic_string(), "mesh/explicit/shared/full.wgsl");
    EXPECT_NE(imports[2].as_asset_path().path.generic_string(), "mesh/shared/root.wgsl");
    EXPECT_NE(imports[3].as_asset_path().path.generic_string(), "common/relative.wgsl");
    EXPECT_NE(imports[3].as_asset_path().path.generic_string(), "shared/relative.wgsl");
}

TEST(ShaderPreprocessSlang, FallsBackToAssetPathWhenNoModuleExists) {
    auto [import_path, imports] = Shader::preprocess_slang("", "shaders/main.slang");

    ASSERT_TRUE(import_path.is_asset_path());
    EXPECT_EQ(import_path.as_asset_path(), AssetPath("shaders/main.slang"));
    EXPECT_TRUE(imports.empty());
}

TEST(ShaderPreprocessSlang, ResolvesAllFourImportFormsToDistinctTargets) {
    const char* source =
        "module scene.main;\n"
        "import utility;\n"
        "import \"embedded://explicit/shared/full\";\n"
        "import \"/shared/root\";\n"
        "__include \"common/relative\";\n";

    auto [import_path, imports] = Shader::preprocess_slang(source, "embedded://mesh/main.slang");

    ASSERT_TRUE(import_path.is_custom());
    EXPECT_EQ(import_path.as_custom(), "scene/main.slang");
    ASSERT_EQ(imports.size(), 4u);

    ASSERT_TRUE(imports[0].is_custom());
    EXPECT_EQ(imports[0].as_custom(), "utility.slang");

    expect_asset_import(imports[1], "embedded", "explicit/shared/full.slang");
    expect_asset_import(imports[2], "embedded", "shared/root.slang");
    expect_asset_import(imports[3], "embedded", "mesh/common/relative.slang");

    EXPECT_NE(imports[1].as_asset_path().path.generic_string(), "mesh/explicit/shared/full.slang");
    EXPECT_NE(imports[2].as_asset_path().path.generic_string(), "mesh/shared/root.slang");
    EXPECT_NE(imports[3].as_asset_path().path.generic_string(), "common/relative.slang");
    EXPECT_NE(imports[3].as_asset_path().path.generic_string(), "shared/relative.slang");
}