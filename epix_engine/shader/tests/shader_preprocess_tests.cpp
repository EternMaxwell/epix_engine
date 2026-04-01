// Tests for Shader preprocessing and factory functions:
//   Shader::preprocess(), Shader::from_wgsl(), Shader::from_wgsl_with_defs(), Shader::from_spirv()

#include <gtest/gtest.h>

import std;
import epix.shader;

using namespace epix::shader;

// ===========================================================================
// Shader::preprocess() — import_path + imports extraction
// ===========================================================================

TEST(ShaderPreprocess, NullSource_FallsBackToPath) {
    auto [ip, imps] = Shader::preprocess("", "shaders/foo.wgsl");
    EXPECT_EQ(ip.kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(ip.path, "shaders/foo.wgsl");
    EXPECT_TRUE(imps.empty());
}

TEST(ShaderPreprocess, DefineImportPath_SetsCustom) {
    const char* src = "#define_import_path my::module::utils\n";
    auto [ip, imps] = Shader::preprocess(src, "shaders/utils.wgsl");
    EXPECT_EQ(ip.kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(ip.path, "my::module::utils");
    EXPECT_TRUE(imps.empty());
}

TEST(ShaderPreprocess, DefineImportPath_LastOneWins) {
    const char* src =
        "#define_import_path first::name\n"
        "#define_import_path second::name\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    EXPECT_EQ(ip.path, "second::name");
}

TEST(ShaderPreprocess, ImportAssetPath_QuotedString) {
    const char* src = "#import \"shaders/common.wgsl\"\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(imps[0].path, "shaders/common.wgsl");
}

TEST(ShaderPreprocess, ImportCustomName) {
    const char* src = "#import my::utils\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(imps[0].path, "my::utils");
}

TEST(ShaderPreprocess, ImportCustomNameWithAlias) {
    // "as alias" part should be stripped — only the module name matters
    const char* src = "#import some::module as alias\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(imps[0].path, "some::module");
}

TEST(ShaderPreprocess, MultipleImports_PreservesOrder) {
    const char* src =
        "#import \"a.wgsl\"\n"
        "#import my::util\n"
        "#import \"b.wgsl\"\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 3u);
    EXPECT_EQ(imps[0].kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(imps[0].path, "a.wgsl");
    EXPECT_EQ(imps[1].kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(imps[1].path, "my::util");
    EXPECT_EQ(imps[2].kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(imps[2].path, "b.wgsl");
}

TEST(ShaderPreprocess, MixedDefineAndImport) {
    const char* src =
        "#define_import_path the::shader\n"
        "#import \"dep.wgsl\"\n"
        "\nfn compute() -> f32 { return 1.0; }\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    EXPECT_EQ(ip.kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(ip.path, "the::shader");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].path, "dep.wgsl");
}

TEST(ShaderPreprocess, IgnoresNonDirectiveLines) {
    const char* src =
        "// This is a comment\n"
        "struct Vertex { pos: vec4<f32> }\n"
        "fn main() {}\n";
    auto [ip, imps] = Shader::preprocess(src, "x.wgsl");
    EXPECT_EQ(ip.kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(ip.path, "x.wgsl");
    EXPECT_TRUE(imps.empty());
}

TEST(ShaderPreprocess, LeadingWhitespaceBeforeDirective) {
    const char* src = "  #define_import_path my::ns\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    EXPECT_EQ(ip.kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(ip.path, "my::ns");
}

TEST(ShaderPreprocess, ImportWithUnclosedQuote_StillParses) {
    // Unclosed quote: grabs everything after the opening quote
    const char* src = "#import \"path.wgsl\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].kind, ShaderImport::Kind::AssetPath);
    // The impl should use whatever string it finds
    EXPECT_FALSE(imps[0].path.empty());
}

// ===========================================================================
// Shader::from_wgsl()
// ===========================================================================

TEST(ShaderFromWgsl, SetsPathAndSource) {
    auto s = Shader::from_wgsl("fn main() {}", "shaders/simple.wgsl");
    EXPECT_EQ(s.path, "shaders/simple.wgsl");
    EXPECT_TRUE(s.source.is_wgsl());
    EXPECT_EQ(s.source.as_str(), "fn main() {}");
}

TEST(ShaderFromWgsl, DefaultImportPath_FallsBackToPath) {
    auto s = Shader::from_wgsl("", "shaders/test.wgsl");
    EXPECT_EQ(s.import_path.kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(s.import_path.path, "shaders/test.wgsl");
}

TEST(ShaderFromWgsl, ImportPathSetByDirective) {
    auto s = Shader::from_wgsl("#define_import_path ns::utils\n", "shaders/utils.wgsl");
    EXPECT_EQ(s.import_path.kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(s.import_path.path, "ns::utils");
}

TEST(ShaderFromWgsl, ImportsExtracted) {
    const char* src = "#import \"dep.wgsl\"\nfn foo() {}";
    auto s          = Shader::from_wgsl(src, "shaders/foo.wgsl");
    ASSERT_EQ(s.imports.size(), 1u);
    EXPECT_EQ(s.imports[0].path, "dep.wgsl");
}

TEST(ShaderFromWgsl, ShaderDefsDefaultEmpty) {
    auto s = Shader::from_wgsl("", "x.wgsl");
    EXPECT_TRUE(s.shader_defs.empty());
}

TEST(ShaderFromWgsl, FileDependenciesDefaultEmpty) {
    auto s = Shader::from_wgsl("", "x.wgsl");
    EXPECT_TRUE(s.file_dependencies.empty());
}

TEST(ShaderFromWgsl, ValidateShaderDefaultDisabled) {
    auto s = Shader::from_wgsl("", "x.wgsl");
    EXPECT_EQ(s.validate_shader, ValidateShader::Disabled);
}

// ===========================================================================
// Shader::from_wgsl_with_defs()
// ===========================================================================

TEST(ShaderFromWgslWithDefs, SetsShaderDefs) {
    std::vector<ShaderDefVal> defs = {
        ShaderDefVal::from_bool("FEATURE_A", true),
        ShaderDefVal::from_int("MAX_LIGHTS", 4),
    };
    auto s = Shader::from_wgsl_with_defs("fn f() {}", "x.wgsl", defs);
    ASSERT_EQ(s.shader_defs.size(), 2u);
    EXPECT_EQ(s.shader_defs[0].name, "FEATURE_A");
    EXPECT_EQ(s.shader_defs[1].name, "MAX_LIGHTS");
    EXPECT_EQ(s.path, "x.wgsl");
    EXPECT_TRUE(s.source.is_wgsl());
}

TEST(ShaderFromWgslWithDefs, EmptyDefs) {
    auto s = Shader::from_wgsl_with_defs("", "x.wgsl", {});
    EXPECT_TRUE(s.shader_defs.empty());
}

TEST(ShaderFromWgslWithDefs, PreprocessStillRunsForDirectives) {
    auto s = Shader::from_wgsl_with_defs("#define_import_path my::ns\n#import \"dep.wgsl\"\n", "x.wgsl",
                                         {ShaderDefVal::from_bool("DEBUG")});
    EXPECT_EQ(s.import_path.kind, ShaderImport::Kind::Custom);
    EXPECT_EQ(s.import_path.path, "my::ns");
    ASSERT_EQ(s.imports.size(), 1u);
}

// ===========================================================================
// Shader::from_spirv()
// ===========================================================================

TEST(ShaderFromSpirv, SetsPathAndSource) {
    std::vector<std::uint8_t> bytecode = {0x03, 0x02, 0x23, 0x07};
    auto s                             = Shader::from_spirv(bytecode, "shaders/compiled.spv");
    EXPECT_EQ(s.path, "shaders/compiled.spv");
    EXPECT_TRUE(s.source.is_spirv());
    EXPECT_FALSE(s.source.is_wgsl());
    const auto& spv = std::get<Source::SpirV>(s.source.data);
    EXPECT_EQ(spv.bytes, bytecode);
}

TEST(ShaderFromSpirv, ImportPathIsAssetPath) {
    auto s = Shader::from_spirv({}, "compiled.spv");
    EXPECT_EQ(s.import_path.kind, ShaderImport::Kind::AssetPath);
    EXPECT_EQ(s.import_path.path, "compiled.spv");
}

TEST(ShaderFromSpirv, ImportsAlwaysEmpty) {
    auto s = Shader::from_spirv({0x01, 0x02, 0x03}, "test.spv");
    EXPECT_TRUE(s.imports.empty());
}

TEST(ShaderFromSpirv, EmptyBytecode) {
    auto s = Shader::from_spirv({}, "empty.spv");
    EXPECT_EQ(s.path, "empty.spv");
    const auto& spv = std::get<Source::SpirV>(s.source.data);
    EXPECT_TRUE(spv.bytes.empty());
}

TEST(ShaderFromSpirv, ShaderDefsDefaultEmpty) {
    auto s = Shader::from_spirv({}, "x.spv");
    EXPECT_TRUE(s.shader_defs.empty());
}
