// Tests for Shader preprocessing and factory functions:
//   Shader::preprocess(), Shader::from_wgsl(), Shader::from_wgsl_with_defs(), Shader::from_spirv()

#include <gtest/gtest.h>

import std;
import epix.assets;
import epix.shader;

using namespace epix::assets;
using namespace epix::shader;

// ===========================================================================
// Shader::preprocess() — import_path + imports extraction
// ===========================================================================

TEST(ShaderPreprocess, NullSource_FallsBackToPath) {
    auto [ip, imps] = Shader::preprocess("", "shaders/foo.wgsl");
    EXPECT_TRUE(ip.is_asset_path());
    EXPECT_EQ(ip.as_asset_path().path.generic_string(), "shaders/foo.wgsl");
    EXPECT_TRUE(imps.empty());
}

TEST(ShaderPreprocess, DefineImportPath_SetsCustom) {
    const char* src = "#define_import_path my::module::utils\n";
    auto [ip, imps] = Shader::preprocess(src, "shaders/utils.wgsl");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "my::module::utils");
    EXPECT_TRUE(imps.empty());
}

TEST(ShaderPreprocess, DefineImportPath_LastOneWins) {
    const char* src =
        "#define_import_path first::name\n"
        "#define_import_path second::name\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    EXPECT_EQ(ip.as_custom(), "second::name");
}

TEST(ShaderPreprocess, ImportAssetPath_QuotedString) {
    const char* src = "#import \"shaders/common.wgsl\"\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_asset_path());
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "shaders/common.wgsl");
}

TEST(ShaderPreprocess, ImportAssetPath_SourceSchemeStripped) {
    // source:// prefix is preserved in AssetPath source component
    const char* src = "#import \"source://shaders/common.wgsl\"\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_asset_path());
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "shaders/common.wgsl");
    ASSERT_TRUE(imps[0].as_asset_path().source.has_value());
    EXPECT_EQ(*imps[0].as_asset_path().source, "source");
}

TEST(ShaderPreprocess, ImportAssetPath_RootRelativePreserved) {
    const char* src = "#import \"/shared/common.wgsl\"\n";
    auto [ip, imps] = Shader::preprocess(src, "embedded://mesh/main.wgsl");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_asset_path());
    EXPECT_TRUE(imps[0].as_asset_path().source.is_default());
    EXPECT_TRUE(imps[0].as_asset_path().path.has_root_directory());
    EXPECT_EQ(imps[0].as_asset_path().path.relative_path().generic_string(), "shared/common.wgsl");
}

TEST(ShaderPreprocess, ImportCustomName) {
    const char* src = "#import my::utils\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "my::utils");
}

TEST(ShaderPreprocess, ImportCustomNameWithAlias) {
    // "as alias" part should be stripped — only the module name matters
    const char* src = "#import some::module as alias\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "some::module");
}

TEST(ShaderPreprocess, MultipleImports_PreservesOrder) {
    const char* src =
        "#import \"a.wgsl\"\n"
        "#import my::util\n"
        "#import \"b.wgsl\"\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 3u);
    EXPECT_TRUE(imps[0].is_asset_path());
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "a.wgsl");
    EXPECT_TRUE(imps[1].is_custom());
    EXPECT_EQ(imps[1].as_custom(), "my::util");
    EXPECT_TRUE(imps[2].is_asset_path());
    EXPECT_EQ(imps[2].as_asset_path().path.generic_string(), "b.wgsl");
}

TEST(ShaderPreprocess, MixedDefineAndImport) {
    const char* src =
        "#define_import_path the::shader\n"
        "#import \"dep.wgsl\"\n"
        "\nfn compute() -> f32 { return 1.0; }\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "the::shader");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "dep.wgsl");
}

TEST(ShaderPreprocess, IgnoresNonDirectiveLines) {
    const char* src =
        "// This is a comment\n"
        "struct Vertex { pos: vec4<f32> }\n"
        "fn main() {}\n";
    auto [ip, imps] = Shader::preprocess(src, "x.wgsl");
    EXPECT_TRUE(ip.is_asset_path());
    EXPECT_EQ(ip.as_asset_path().path.generic_string(), "x.wgsl");
    EXPECT_TRUE(imps.empty());
}

TEST(ShaderPreprocess, LeadingWhitespaceBeforeDirective) {
    const char* src = "  #define_import_path my::ns\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "my::ns");
}

TEST(ShaderPreprocess, ImportWithUnclosedQuote_StillParses) {
    // Unclosed quote: grabs everything after the opening quote
    const char* src = "#import \"path.wgsl\n";
    auto [ip, imps] = Shader::preprocess(src, "path");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_asset_path());
    // The impl should use whatever string it finds
    EXPECT_FALSE(imps[0].as_asset_path().path.generic_string().empty());
}

// ===========================================================================
// Shader::from_wgsl()
// ===========================================================================

TEST(ShaderFromWgsl, SetsPathAndSource) {
    auto s = Shader::from_wgsl("fn main() {}", "shaders/simple.wgsl");
    EXPECT_EQ(s.path, AssetPath("shaders/simple.wgsl"));
    EXPECT_TRUE(s.source.is_wgsl());
    EXPECT_EQ(s.source.as_str(), "fn main() {}");
}

TEST(ShaderFromWgsl, PreservesAssetSourceInPathAndFallbackImportPath) {
    auto s = Shader::from_wgsl("", "embedded://shaders/simple.wgsl");
    EXPECT_EQ(s.path, AssetPath("embedded://shaders/simple.wgsl"));
    EXPECT_TRUE(s.import_path.is_asset_path());
    EXPECT_EQ(s.import_path.as_asset_path(), AssetPath("embedded://shaders/simple.wgsl"));
}

TEST(ShaderFromWgsl, DefaultImportPath_FallsBackToPath) {
    auto s = Shader::from_wgsl("", "shaders/test.wgsl");
    EXPECT_TRUE(s.import_path.is_asset_path());
    EXPECT_EQ(s.import_path.as_asset_path().path.generic_string(), "shaders/test.wgsl");
}

TEST(ShaderFromWgsl, ImportPathSetByDirective) {
    auto s = Shader::from_wgsl("#define_import_path ns::utils\n", "shaders/utils.wgsl");
    EXPECT_TRUE(s.import_path.is_custom());
    EXPECT_EQ(s.import_path.as_custom(), "ns::utils");
}

TEST(ShaderFromWgsl, ImportsExtracted) {
    const char* src = "#import \"dep.wgsl\"\nfn foo() {}";
    auto s          = Shader::from_wgsl(src, "shaders/foo.wgsl");
    ASSERT_EQ(s.imports.size(), 1u);
    EXPECT_EQ(s.imports[0].as_asset_path().path.generic_string(), "dep.wgsl");
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
    EXPECT_EQ(s.path, AssetPath("x.wgsl"));
    EXPECT_TRUE(s.source.is_wgsl());
}

TEST(ShaderFromWgslWithDefs, EmptyDefs) {
    auto s = Shader::from_wgsl_with_defs("", "x.wgsl", {});
    EXPECT_TRUE(s.shader_defs.empty());
}

TEST(ShaderFromWgslWithDefs, PreprocessStillRunsForDirectives) {
    auto s = Shader::from_wgsl_with_defs("#define_import_path my::ns\n#import \"dep.wgsl\"\n", "x.wgsl",
                                         {ShaderDefVal::from_bool("DEBUG")});
    EXPECT_TRUE(s.import_path.is_custom());
    EXPECT_EQ(s.import_path.as_custom(), "my::ns");
    ASSERT_EQ(s.imports.size(), 1u);
}

// ===========================================================================
// Shader::from_spirv()
// ===========================================================================

TEST(ShaderFromSpirv, SetsPathAndSource) {
    std::vector<std::uint8_t> bytecode = {0x03, 0x02, 0x23, 0x07};
    auto s                             = Shader::from_spirv(bytecode, "shaders/compiled.spv");
    EXPECT_EQ(s.path, AssetPath("shaders/compiled.spv"));
    EXPECT_TRUE(s.source.is_spirv());
    EXPECT_FALSE(s.source.is_wgsl());
    const auto& spv = std::get<Source::SpirV>(s.source.data);
    EXPECT_EQ(spv.bytes, bytecode);
}

TEST(ShaderFromSpirv, ImportPathIsAssetPath) {
    auto s = Shader::from_spirv({}, "compiled.spv");
    EXPECT_TRUE(s.import_path.is_asset_path());
    EXPECT_EQ(s.import_path.as_asset_path().path.generic_string(), "compiled.spv");
}

TEST(ShaderFromSpirv, ImportsAlwaysEmpty) {
    auto s = Shader::from_spirv({0x01, 0x02, 0x03}, "test.spv");
    EXPECT_TRUE(s.imports.empty());
}

TEST(ShaderFromSpirv, EmptyBytecode) {
    auto s = Shader::from_spirv({}, "empty.spv");
    EXPECT_EQ(s.path, AssetPath("empty.spv"));
    const auto& spv = std::get<Source::SpirV>(s.source.data);
    EXPECT_TRUE(spv.bytes.empty());
}

TEST(ShaderFromSpirv, ShaderDefsDefaultEmpty) {
    auto s = Shader::from_spirv({}, "x.spv");
    EXPECT_TRUE(s.shader_defs.empty());
}

// ===========================================================================
// Shader::from_slang()
// ===========================================================================

TEST(ShaderFromSlang, SetsPathAndSource) {
    auto s = Shader::from_slang("[shader(\"vertex\")] float4 main() : SV_Position { return 0; }", "shaders/test.slang");
    EXPECT_EQ(s.path, AssetPath("shaders/test.slang"));
    EXPECT_TRUE(s.source.is_slang());
    EXPECT_FALSE(s.source.is_wgsl());
    EXPECT_FALSE(s.source.is_spirv());
    EXPECT_EQ(s.source.as_str(), "[shader(\"vertex\")] float4 main() : SV_Position { return 0; }");
}

TEST(ShaderFromSlang, ImportPathIsAssetPath) {
    auto s = Shader::from_slang("", "test.slang");
    EXPECT_TRUE(s.import_path.is_asset_path());
    EXPECT_EQ(s.import_path.as_asset_path().path.generic_string(), "test.slang");
}

TEST(ShaderFromSlang, ImportsExtractedFromImportStatements) {
    // Slang's `import X;` is now parsed to populate the imports vector
    // Underscores in identifier tokens → hyphens per Slang spec
    auto s = Shader::from_slang("import some_module;\nvoid main() {}", "test.slang");
    ASSERT_EQ(s.imports.size(), 1u);
    EXPECT_TRUE(s.imports[0].is_custom());
    EXPECT_EQ(s.imports[0].as_custom(), "some-module.slang");
}

TEST(ShaderFromSlang, NoImportStatements_ImportsEmpty) {
    auto s = Shader::from_slang("[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid computeMain() {}", "test.slang");
    EXPECT_TRUE(s.imports.empty());
}

TEST(ShaderFromSlang, ShaderDefsDefaultEmpty) {
    auto s = Shader::from_slang("", "x.slang");
    EXPECT_TRUE(s.shader_defs.empty());
}

TEST(ShaderFromSlang, ValidateShaderDefaultDisabled) {
    auto s = Shader::from_slang("", "x.slang");
    EXPECT_EQ(s.validate_shader, ValidateShader::Disabled);
}

// ===========================================================================
// Shader::from_slang_with_defs()
// ===========================================================================

TEST(ShaderFromSlangWithDefs, SetsShaderDefs) {
    std::vector<ShaderDefVal> defs = {
        ShaderDefVal::from_bool("USE_NORMAL_MAP", true),
        ShaderDefVal::from_int("MAX_LIGHTS", 8),
    };
    auto s = Shader::from_slang_with_defs("void main() {}", "x.slang", defs);
    ASSERT_EQ(s.shader_defs.size(), 2u);
    EXPECT_EQ(s.shader_defs[0].name, "USE_NORMAL_MAP");
    EXPECT_EQ(s.shader_defs[1].name, "MAX_LIGHTS");
    EXPECT_EQ(s.path, AssetPath("x.slang"));
    EXPECT_TRUE(s.source.is_slang());
}

TEST(ShaderFromSlangWithDefs, EmptyDefs) {
    auto s = Shader::from_slang_with_defs("", "x.slang", {});
    EXPECT_TRUE(s.shader_defs.empty());
    EXPECT_TRUE(s.source.is_slang());
}

// ===========================================================================
// Shader::preprocess_slang() — import extraction from Slang source
// ===========================================================================

TEST(SlangPreprocess, EmptySource_NoImports) {
    auto [ip, imps] = Shader::preprocess_slang("", "test.slang");
    EXPECT_TRUE(ip.is_asset_path());
    EXPECT_EQ(ip.as_asset_path().path.generic_string(), "test.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, SingleImport) {
    auto [ip, imps] = Shader::preprocess_slang("import utility;\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "utility.slang");
}

TEST(SlangPreprocess, DottedImport_ConvertedToPath) {
    // import some.nested.module; → some/nested/module.slang
    auto [ip, imps] = Shader::preprocess_slang("import some.nested.module;\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "some/nested/module.slang");
}

TEST(SlangPreprocess, MultipleImports_PreservesOrder) {
    const char* src =
        "import alpha;\n"
        "import beta.gamma;\n"
        "import delta;\n";
    auto [ip, imps] = Shader::preprocess_slang(src, "test.slang");
    ASSERT_EQ(imps.size(), 3u);
    EXPECT_EQ(imps[0].as_custom(), "alpha.slang");
    EXPECT_EQ(imps[1].as_custom(), "beta/gamma.slang");
    EXPECT_EQ(imps[2].as_custom(), "delta.slang");
}

TEST(SlangPreprocess, ImportWithExtraWhitespace) {
    auto [ip, imps] = Shader::preprocess_slang("  import   utility  ;\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "utility.slang");
}

TEST(SlangPreprocess, ImportMissingName_Skipped) {
    auto [ip, imps] = Shader::preprocess_slang("import ;\n", "test.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, ImportWithoutSemicolon_StillParses) {
    // Gracefully handle missing semicolon
    auto [ip, imps] = Shader::preprocess_slang("import utility\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "utility.slang");
}

TEST(SlangPreprocess, CommentedOutImport_Skipped) {
    auto [ip, imps] = Shader::preprocess_slang("// import utility;\n", "test.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, NonImportLines_Skipped) {
    const char* src =
        "struct Vertex { float3 pos; };\n"
        "[shader(\"compute\")]\n"
        "[numthreads(1,1,1)]\n"
        "void computeMain() {}\n";
    auto [ip, imps] = Shader::preprocess_slang(src, "test.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, MixedImportsAndCode) {
    const char* src =
        "import utility;\n"
        "\n"
        "struct Foo { float x; };\n"
        "import math.vec;\n"
        "\n"
        "[shader(\"compute\")]\n"
        "[numthreads(1,1,1)]\n"
        "void computeMain() {}\n";
    auto [ip, imps] = Shader::preprocess_slang(src, "main.slang");
    ASSERT_EQ(imps.size(), 2u);
    EXPECT_EQ(imps[0].as_custom(), "utility.slang");
    EXPECT_EQ(imps[1].as_custom(), "math/vec.slang");
}

TEST(SlangPreprocess, ImportPath_AlwaysAssetPath) {
    auto [ip, imps] = Shader::preprocess_slang("", "shaders/compute.slang");
    EXPECT_TRUE(ip.is_asset_path());
    EXPECT_EQ(ip.as_asset_path().path.generic_string(), "shaders/compute.slang");
}

TEST(SlangPreprocess, ImportKeywordInString_NotTreatedAsImport) {
    // The word "import" in a string literal on a line that doesn't start with "import " keyword
    const char* src = "const char* s = \"import foo\";\n";
    auto [ip, imps] = Shader::preprocess_slang(src, "test.slang");
    EXPECT_TRUE(imps.empty());
}

// ===========================================================================
// Shader::from_slang() — imports populated
// ===========================================================================

TEST(ShaderFromSlang, SingleImportIdentifier_PopulatesImports) {
    auto s = Shader::from_slang("import utility;\nvoid main() {}", "main.slang");
    ASSERT_EQ(s.imports.size(), 1u);
    EXPECT_TRUE(s.imports[0].is_custom());
    EXPECT_EQ(s.imports[0].as_custom(), "utility.slang");
}

TEST(ShaderFromSlang, DottedImport_PopulatesImports) {
    auto s = Shader::from_slang("import a.b.c;\nvoid main() {}", "main.slang");
    ASSERT_EQ(s.imports.size(), 1u);
    EXPECT_TRUE(s.imports[0].is_custom());
    EXPECT_EQ(s.imports[0].as_custom(), "a/b/c.slang");
}

TEST(ShaderFromSlang, MultipleImports_PopulatesAll) {
    auto s = Shader::from_slang("import foo;\nimport bar.baz;\nvoid main() {}", "main.slang");
    ASSERT_EQ(s.imports.size(), 2u);
    EXPECT_EQ(s.imports[0].as_custom(), "foo.slang");
    EXPECT_EQ(s.imports[1].as_custom(), "bar/baz.slang");
}

TEST(ShaderFromSlang, ImportPath_IsAssetPathOfFilePath) {
    auto s = Shader::from_slang("import utility;\nvoid main() {}", "shaders/main.slang");
    EXPECT_TRUE(s.import_path.is_asset_path());
    EXPECT_EQ(s.import_path.as_asset_path().path.generic_string(), "shaders/main.slang");
}

TEST(ShaderFromSlangWithDefs, ImportsStillParsed) {
    auto s = Shader::from_slang_with_defs("import utility;\nvoid main() {}", "main.slang",
                                          {ShaderDefVal::from_bool("DEBUG")});
    ASSERT_EQ(s.imports.size(), 1u);
    EXPECT_TRUE(s.imports[0].is_custom());
    EXPECT_EQ(s.imports[0].as_custom(), "utility.slang");
    ASSERT_EQ(s.shader_defs.size(), 1u);
    EXPECT_EQ(s.shader_defs[0].name, "DEBUG");
}

// ===========================================================================
// Slang preprocess — __include support
// ===========================================================================

TEST(SlangPreprocess, Include_Identifier) {
    auto [ip, imps] = Shader::preprocess_slang("__include scene_helpers;\n", "main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "scene-helpers.slang");
}

TEST(SlangPreprocess, Include_DottedIdentifier) {
    auto [ip, imps] = Shader::preprocess_slang("__include scene.helpers_util;\n", "main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "scene/helpers-util.slang");
}

TEST(SlangPreprocess, Include_StringLiteral) {
    auto [ip, imps] = Shader::preprocess_slang("__include \"scene-helpers.slang\";\n", "main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "scene-helpers.slang");
}

TEST(SlangPreprocess, Include_StringLiteralNoExtension) {
    // .slang appended automatically when no extension present
    auto [ip, imps] = Shader::preprocess_slang("__include \"scene-helpers\";\n", "main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "scene-helpers.slang");
}

TEST(SlangPreprocess, Include_MissingName_Skipped) {
    auto [ip, imps] = Shader::preprocess_slang("__include ;\n", "main.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, Include_WithExtraWhitespace) {
    auto [ip, imps] = Shader::preprocess_slang("  __include   helpers  ;\n", "main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "helpers.slang");
}

// ===========================================================================
// Slang preprocess — module declaration
// ===========================================================================

TEST(SlangPreprocess, ModuleDeclaration_SetsImportPath) {
    auto [ip, imps] = Shader::preprocess_slang("module scene;\nvoid draw() {}\n", "shaders/scene.slang");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "scene.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, ModuleDeclaration_DottedName) {
    auto [ip, imps] = Shader::preprocess_slang("module graphics.utils;\n", "x.slang");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "graphics/utils.slang");
}

TEST(SlangPreprocess, ModuleDeclaration_Underscore) {
    auto [ip, imps] = Shader::preprocess_slang("module my_lib;\n", "x.slang");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "my-lib.slang");
}

TEST(SlangPreprocess, ModuleDeclaration_StringLiteralNoExtension_CanonicalizedToCustomName) {
    auto [ip, imps] = Shader::preprocess_slang("module \"common/view\";\n", "embedded://mesh/main.slang");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "common/view.slang");
    EXPECT_TRUE(imps.empty());
}

TEST(SlangPreprocess, NoModuleDeclaration_ImportPathIsFallback) {
    auto [ip, imps] = Shader::preprocess_slang("import foo;\n", "shaders/main.slang");
    EXPECT_EQ(ip.as_asset_path().path.generic_string(), "shaders/main.slang");
}

// ===========================================================================
// Slang preprocess — string-literal import syntax
// ===========================================================================

TEST(SlangPreprocess, ImportStringLiteral) {
    auto [ip, imps] = Shader::preprocess_slang("import \"path/to/file.slang\";\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "path/to/file.slang");
}

TEST(SlangPreprocess, ImportStringLiteral_NoExtension) {
    auto [ip, imps] = Shader::preprocess_slang("import \"path/to/file\";\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "path/to/file.slang");
}

// ===========================================================================
// Slang preprocess — underscore→hyphen translation
// ===========================================================================

TEST(SlangPreprocess, UnderscoreToHyphen_Import) {
    auto [ip, imps] = Shader::preprocess_slang("import my_lib;\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "my-lib.slang");
}

TEST(SlangPreprocess, UnderscoreToHyphen_DottedImport) {
    // import dir.file_name; → dir/file-name.slang
    auto [ip, imps] = Shader::preprocess_slang("import some_dir.my_module;\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "some-dir/my-module.slang");
}

TEST(SlangPreprocess, UnderscoreToHyphen_Include) {
    auto [ip, imps] = Shader::preprocess_slang("__include scene_helpers;\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "scene-helpers.slang");
}

// ===========================================================================
// Slang preprocess — implementing declaration (no dependency)
// ===========================================================================

TEST(SlangPreprocess, ImplementingDeclaration_NotAnImport) {
    // `implementing X;` just declares the file is part of module X, not a dependency
    auto [ip, imps] = Shader::preprocess_slang("implementing scene;\nvoid helper() {}\n", "helper.slang");
    EXPECT_TRUE(imps.empty());
}

// ===========================================================================
// Slang preprocess — mixed import + __include + module
// ===========================================================================

TEST(SlangPreprocess, Mixed_ImportAndInclude) {
    const char* src =
        "module my_app;\n"
        "import graphics.utils;\n"
        "__include app_helpers;\n"
        "import \"third-party/lib.slang\";\n"
        "void main() {}\n";
    auto [ip, imps] = Shader::preprocess_slang(src, "app.slang");
    EXPECT_TRUE(ip.is_custom());
    EXPECT_EQ(ip.as_custom(), "my-app.slang");
    ASSERT_EQ(imps.size(), 3u);
    EXPECT_TRUE(imps[0].is_custom());
    EXPECT_EQ(imps[0].as_custom(), "graphics/utils.slang");
    EXPECT_TRUE(imps[1].is_custom());
    EXPECT_EQ(imps[1].as_custom(), "app-helpers.slang");
    EXPECT_EQ(imps[2].as_asset_path().path.generic_string(), "third-party/lib.slang");
}

TEST(SlangPreprocess, StringLiteral_NoUnderscoreTranslation) {
    // String-literal paths should NOT have underscore→hyphen translation
    auto [ip, imps] = Shader::preprocess_slang("import \"my_file\";\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "my_file.slang");
}

TEST(SlangPreprocess, ImportStringLiteral_SourceSchemeStripped) {
    // source:// prefix is preserved in AssetPath source component
    auto [ip, imps] = Shader::preprocess_slang("import \"my-source://shaders/dep.slang\";\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    ASSERT_TRUE(imps[0].as_asset_path().source.as_str().has_value());
    EXPECT_EQ(imps[0].as_asset_path().source.as_str().value(), "my-source");
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "shaders/dep.slang");
}

TEST(SlangPreprocess, IncludeStringLiteral_SourceSchemeStripped) {
    auto [ip, imps] = Shader::preprocess_slang("__include \"assets://helpers/scene.slang\";\n", "test.slang");
    ASSERT_EQ(imps.size(), 1u);
    ASSERT_TRUE(imps[0].as_asset_path().source.as_str().has_value());
    EXPECT_EQ(imps[0].as_asset_path().source.as_str().value(), "assets");
    EXPECT_EQ(imps[0].as_asset_path().path.generic_string(), "helpers/scene.slang");
}

TEST(SlangPreprocess, IncludeStringLiteral_RootRelativeSameSource) {
    auto [ip, imps] = Shader::preprocess_slang("__include \"/shared/view\";\n", "embedded://mesh/main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_asset_path());
    EXPECT_TRUE(imps[0].as_asset_path().source.is_default());
    EXPECT_TRUE(imps[0].as_asset_path().path.has_root_directory());
    EXPECT_EQ(imps[0].as_asset_path().path.relative_path().generic_string(), "shared/view.slang");
}

TEST(SlangPreprocess, ImportStringLiteral_RootRelativeSameSource) {
    auto [ip, imps] = Shader::preprocess_slang("import \"/shared/view\";\n", "embedded://mesh/main.slang");
    ASSERT_EQ(imps.size(), 1u);
    EXPECT_TRUE(imps[0].is_asset_path());
    EXPECT_TRUE(imps[0].as_asset_path().source.is_default());
    EXPECT_TRUE(imps[0].as_asset_path().path.has_root_directory());
    EXPECT_EQ(imps[0].as_asset_path().path.relative_path().generic_string(), "shared/view.slang");
}
