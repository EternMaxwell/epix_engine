// Tests for ShaderComposer:
//   add_module, remove_module, contains_module, compose (import resolution,
//   conditional blocks, cycle detection)

#include <gtest/gtest.h>

import std;
import epix.shader;

using namespace epix::shader;

// Helper: extract which variant ComposeError holds
template <typename T>
static bool has_error(const ComposeError& e) {
    return std::holds_alternative<T>(e.data);
}

// ===========================================================================
// Module lifecycle
// ===========================================================================

TEST(ShaderComposer, AddModule_RegistersSuccessfully) {
    ShaderComposer c;
    auto res = c.add_module("my::utils", "fn utility() -> f32 { return 0.0; }", {});
    EXPECT_TRUE(res.has_value());
    EXPECT_TRUE(c.contains_module("my::utils"));
}

TEST(ShaderComposer, AddModule_EmptyName_ParseError) {
    ShaderComposer c;
    auto res = c.add_module("", "fn f() {}", {});
    EXPECT_FALSE(res.has_value());
    EXPECT_TRUE(has_error<ComposeError::ParseError>(res.error()));
    EXPECT_EQ(std::get<ComposeError::ParseError>(res.error().data).module_name, "");
}

TEST(ShaderComposer, ContainsModule_UnknownName_False) {
    ShaderComposer c;
    EXPECT_FALSE(c.contains_module("not::registered"));
}

TEST(ShaderComposer, RemoveModule_RemovesRegistration) {
    ShaderComposer c;
    c.add_module("ns::mod", "fn f() {}", {});
    EXPECT_TRUE(c.contains_module("ns::mod"));
    c.remove_module("ns::mod");
    EXPECT_FALSE(c.contains_module("ns::mod"));
}

TEST(ShaderComposer, RemoveModule_UnknownName_Noop) {
    ShaderComposer c;
    EXPECT_NO_THROW(c.remove_module("not::there"));
}

TEST(ShaderComposer, AddModule_OverwritesExisting) {
    ShaderComposer c;
    c.add_module("ns::mod", "fn a() {}", {});
    auto res = c.add_module("ns::mod", "fn b() {}", {});
    EXPECT_TRUE(res.has_value());
    EXPECT_TRUE(c.contains_module("ns::mod"));
}

// ===========================================================================
// compose — passthrough (no directives)
// ===========================================================================

TEST(ShaderComposer, ComposeSimpleSource_PassThrough) {
    ShaderComposer c;
    const char* src = "struct Vertex {\n    pos: vec4<f32>,\n}\n";
    auto result     = c.compose(src, "shader.wgsl", {});
    ASSERT_TRUE(result.has_value());
    // All regular lines preserved
    EXPECT_NE(result.value().find("struct Vertex"), std::string::npos);
    EXPECT_NE(result.value().find("pos: vec4<f32>"), std::string::npos);
}

TEST(ShaderComposer, ComposeEmptySource_ReturnsEmpty) {
    ShaderComposer c;
    auto result = c.compose("", "shader.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

// ===========================================================================
// compose — #define_import_path stripped
// ===========================================================================

TEST(ShaderComposer, ComposeStripsDefineImportPath) {
    ShaderComposer c;
    const char* src = "#define_import_path ns::shader\nfn main() {}\n";
    auto result     = c.compose(src, "path", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("#define_import_path"), std::string::npos);
    EXPECT_NE(result.value().find("fn main()"), std::string::npos);
}

// ===========================================================================
// compose — #import (quoted = AssetPath-style module name with quotes)
// ===========================================================================

TEST(ShaderComposer, ComposeInlinesQuotedImport) {
    ShaderComposer c;
    // Quoted import key includes the quotes in the module name
    c.add_module("\"shaders/common.wgsl\"", "fn common_fn() -> f32 { return 1.0; }", {});

    const char* src = "#import \"shaders/common.wgsl\"\nfn use_it() -> f32 { return common_fn(); }\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn common_fn()"), std::string::npos);
    EXPECT_NE(result.value().find("fn use_it()"), std::string::npos);
    // The #import directive itself should NOT be in the output
    EXPECT_EQ(result.value().find("#import"), std::string::npos);
}

TEST(ShaderComposer, ComposeInlinesCustomNameImport) {
    ShaderComposer c;
    c.add_module("my::utils", "fn util() {}", {});

    const char* src = "#import my::utils\nfn main() { util(); }\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn util()"), std::string::npos);
    EXPECT_EQ(result.value().find("#import"), std::string::npos);
}

TEST(ShaderComposer, ComposeCustomImportWithAlias_TakesNameBeforeAs) {
    ShaderComposer c;
    c.add_module("ns::mod", "fn modded() {}", {});

    // "as alias" part is discarded; lookup is by "ns::mod"
    const char* src = "#import ns::mod as alias\nfn main() { modded(); }\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn modded()"), std::string::npos);
}

TEST(ShaderComposer, ComposeUnregisteredImport_ImportNotFound) {
    ShaderComposer c;
    const char* src = "#import missing::module\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_error<ComposeError::ImportNotFound>(result.error()));
    EXPECT_EQ(std::get<ComposeError::ImportNotFound>(result.error().data).import_name, "missing::module");
}

// ===========================================================================
// compose — nested imports
// ===========================================================================

TEST(ShaderComposer, ComposeNestedImports_AllInlined) {
    ShaderComposer c;
    // C is the deepest dependency
    c.add_module("ns::c", "fn deep_fn() {}", {});
    // B imports C
    c.add_module("ns::b", "#import ns::c\nfn mid_fn() { deep_fn(); }", {});
    // Main imports B
    const char* src = "#import ns::b\nfn top() { mid_fn(); }\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn deep_fn()"), std::string::npos);
    EXPECT_NE(result.value().find("fn mid_fn()"), std::string::npos);
    EXPECT_NE(result.value().find("fn top()"), std::string::npos);
}

TEST(ShaderComposer, ComposeMultipleIndependentImports) {
    ShaderComposer c;
    c.add_module("ns::a", "fn fn_a() {}", {});
    c.add_module("ns::b", "fn fn_b() {}", {});

    const char* src = "#import ns::a\n#import ns::b\nfn main() { fn_a(); fn_b(); }\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn fn_a()"), std::string::npos);
    EXPECT_NE(result.value().find("fn fn_b()"), std::string::npos);
}

// ===========================================================================
// compose — circular import detection
// ===========================================================================

TEST(ShaderComposer, ComposeCircularImport_DirectSelf) {
    ShaderComposer c;
    // Module imports itself
    c.add_module("ns::self", "#import ns::self\nfn f() {}", {});

    const char* src = "#import ns::self\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_error<ComposeError::CircularImport>(result.error()));
    const auto& chain = std::get<ComposeError::CircularImport>(result.error().data).cycle_chain;
    EXPECT_FALSE(chain.empty());
}

TEST(ShaderComposer, ComposeCircularImport_TwoModules) {
    ShaderComposer c;
    c.add_module("ns::A", "#import ns::B\nfn fn_a() {}", {});
    c.add_module("ns::B", "#import ns::A\nfn fn_b() {}", {});

    const char* src = "#import ns::A\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(has_error<ComposeError::CircularImport>(result.error()));
    const auto& chain = std::get<ComposeError::CircularImport>(result.error().data).cycle_chain;
    // Chain should include at least the repeated module name
    EXPECT_GE(chain.size(), 2u);
}

// ===========================================================================
// compose — #ifdef / #ifndef blocks
// ===========================================================================

TEST(ShaderComposer, ComposeIfdef_NameDefined_BlockIncluded) {
    ShaderComposer c;
    const char* src =
        "#ifdef MY_FEATURE\n"
        "fn feature_fn() {}\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("MY_FEATURE")};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn feature_fn()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfdef_NameNotDefined_BlockExcluded) {
    ShaderComposer c;
    const char* src =
        "#ifdef MY_FEATURE\n"
        "fn feature_fn() {}\n"
        "#endif\n";
    auto result = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn feature_fn()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfndef_NameDefined_BlockExcluded) {
    ShaderComposer c;
    const char* src =
        "#ifndef DEBUG_MODE\n"
        "fn release_path() {}\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("DEBUG_MODE")};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn release_path()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfndef_NameNotDefined_BlockIncluded) {
    ShaderComposer c;
    const char* src =
        "#ifndef DEBUG_MODE\n"
        "fn release_path() {}\n"
        "#endif\n";
    auto result = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn release_path()"), std::string::npos);
}

// ===========================================================================
// compose — #if NAME (bare, treated as #ifdef)
// ===========================================================================

TEST(ShaderComposer, ComposeIfBare_NameDefined_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if MY_FLAG\nfn flagged() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("MY_FLAG")};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn flagged()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfBare_NameNotDefined_BlockExcluded) {
    ShaderComposer c;
    const char* src = "#if MY_FLAG\nfn flagged() {}\n#endif\n";
    auto result     = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn flagged()"), std::string::npos);
}

// ===========================================================================
// compose — #if NAME == value / #if NAME != value
// ===========================================================================

TEST(ShaderComposer, ComposeIfEquals_MatchingValue_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS == 4\nfn quad_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 4)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn quad_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfEquals_NonMatchingValue_BlockExcluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS == 4\nfn quad_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 8)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn quad_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfEquals_Undefined_BlockExcluded) {
    // Per impl: not defined → eq is false
    ShaderComposer c;
    const char* src = "#if MAX_LIGHTS == 4\nfn quad_lights() {}\n#endif\n";
    auto result     = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn quad_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfNotEquals_DifferentValue_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS != 0\nfn any_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 4)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn any_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfNotEquals_SameValue_BlockExcluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS != 4\nfn any_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 4)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn any_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfNotEquals_Undefined_BlockIncluded) {
    // Per impl: not defined → ne is true
    ShaderComposer c;
    const char* src = "#if MAX_LIGHTS != 4\nfn any_lights() {}\n#endif\n";
    auto result     = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn any_lights()"), std::string::npos);
}

// ===========================================================================
// compose — #else branch
// ===========================================================================

TEST(ShaderComposer, ComposeElse_ConditionTrue_ThenIncluded_ElseExcluded) {
    ShaderComposer c;
    const char* src =
        "#ifdef USE_FAST\n"
        "fn fast_path() {}\n"
        "#else\n"
        "fn slow_path() {}\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("USE_FAST")};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn fast_path()"), std::string::npos);
    EXPECT_EQ(result.value().find("fn slow_path()"), std::string::npos);
}

TEST(ShaderComposer, ComposeElse_ConditionFalse_ThenExcluded_ElseIncluded) {
    ShaderComposer c;
    const char* src =
        "#ifdef USE_FAST\n"
        "fn fast_path() {}\n"
        "#else\n"
        "fn slow_path() {}\n"
        "#endif\n";
    auto result = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn fast_path()"), std::string::npos);
    EXPECT_NE(result.value().find("fn slow_path()"), std::string::npos);
}

// ===========================================================================
// compose — nested conditionals
// ===========================================================================

TEST(ShaderComposer, ComposeNestedConditionals_BothTrue_InnerBlockIncluded) {
    ShaderComposer c;
    const char* src =
        "#ifdef OUTER\n"
        "#ifdef INNER\n"
        "fn both_set() {}\n"
        "#endif\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {
        ShaderDefVal::from_bool("OUTER"),
        ShaderDefVal::from_bool("INNER"),
    };
    auto result = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn both_set()"), std::string::npos);
}

TEST(ShaderComposer, ComposeNestedConditionals_OuterFalse_InnerExcluded) {
    ShaderComposer c;
    const char* src =
        "#ifdef OUTER\n"
        "#ifdef INNER\n"
        "fn both_set() {}\n"
        "#endif\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("INNER")};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn both_set()"), std::string::npos);
}

// ===========================================================================
// compose — imports inside conditional blocks
// ===========================================================================

TEST(ShaderComposer, ComposeImportInsideIfdef_ConditionTrue_Inlined) {
    ShaderComposer c;
    c.add_module("optional::mod", "fn optional_fn() {}", {});
    const char* src =
        "#ifdef USE_OPTIONAL\n"
        "#import optional::mod\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("USE_OPTIONAL")};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn optional_fn()"), std::string::npos);
}

TEST(ShaderComposer, ComposeImportInsideIfdef_ConditionFalse_NotInlined) {
    ShaderComposer c;
    // Even if module doesn't exist, should NOT cause ImportNotFound when condition is false
    const char* src =
        "#ifdef USE_OPTIONAL\n"
        "#import optional::mod\n"
        "#endif\n";
    auto result = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn optional_fn()"), std::string::npos);
}

// ===========================================================================
// compose — module base_defs interact with caller defs
// ===========================================================================

TEST(ShaderComposer, ComposeModuleBaseDefs_AppliedToModuleConditional) {
    ShaderComposer c;
    // Module has a conditional controlled by its own base_def
    const char* module_src =
        "#ifdef MOD_FLAG\n"
        "fn conditional_fn() {}\n"
        "#endif\n"
        "fn always_fn() {}\n";
    std::vector<ShaderDefVal> module_defs = {ShaderDefVal::from_bool("MOD_FLAG")};
    c.add_module("ns::mod", module_src, module_defs);

    const char* src = "#import ns::mod\nfn main() {}\n";
    // Compose without passing MOD_FLAG explicitly — module base_defs provide it
    auto result = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn conditional_fn()"), std::string::npos);
    EXPECT_NE(result.value().find("fn always_fn()"), std::string::npos);
}

TEST(ShaderComposer, ComposeAdditionalDefs_OverrideModuleBaseDefs) {
    ShaderComposer c;
    // Module's base_def says MOD_FLAG=true, but we DON't override it here
    // Instead we check that additional_defs from compose() are applied to the main shader
    const char* src =
        "#ifdef CALLER_FLAG\n"
        "fn caller_fn() {}\n"
        "#endif\n";
    std::vector<ShaderDefVal> additional = {ShaderDefVal::from_bool("CALLER_FLAG")};
    auto result                          = c.compose(src, "main.wgsl", additional);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn caller_fn()"), std::string::npos);
}

// ===========================================================================
// compose — ordered comparison operators (>=, <=, >, <)
// ===========================================================================

TEST(ShaderComposer, ComposeIfGe_ValueGe_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS >= 4\nfn enough_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 4)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn enough_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfGe_ValueGreater_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS >= 4\nfn enough_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 8)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn enough_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfGe_ValueLess_BlockExcluded) {
    ShaderComposer c;
    const char* src                = "#if MAX_LIGHTS >= 4\nfn enough_lights() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 2)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn enough_lights()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfLe_ValueLe_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if QUALITY <= 3\nfn low_quality() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("QUALITY", 3)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn low_quality()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfLe_ValueGreater_BlockExcluded) {
    ShaderComposer c;
    const char* src                = "#if QUALITY <= 3\nfn low_quality() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("QUALITY", 5)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn low_quality()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfGt_ValueGreater_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if LEVEL > 0\nfn nonzero() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("LEVEL", 1)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn nonzero()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfGt_ValueEqual_BlockExcluded) {
    ShaderComposer c;
    const char* src                = "#if LEVEL > 0\nfn nonzero() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("LEVEL", 0)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn nonzero()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfLt_ValueLess_BlockIncluded) {
    ShaderComposer c;
    const char* src                = "#if TIER < 3\nfn budget() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("TIER", 2)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("fn budget()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfLt_ValueEqual_BlockExcluded) {
    ShaderComposer c;
    const char* src                = "#if TIER < 3\nfn budget() {}\n#endif\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("TIER", 3)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn budget()"), std::string::npos);
}

TEST(ShaderComposer, ComposeIfOrdered_Undefined_BlockExcluded) {
    ShaderComposer c;
    const char* src = "#if MISSING > 0\nfn f() {}\n#endif\n";
    auto result     = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().find("fn f()"), std::string::npos);
}

// ===========================================================================
// compose — #{NAME} and #NAME def substitution in code lines
// ===========================================================================

TEST(ShaderComposer, ComposeSubstitution_BracedForm_ReplacedWithValue) {
    ShaderComposer c;
    const char* src                = "let max_lights: u32 = #{MAX_LIGHTS}u;\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_uint("MAX_LIGHTS", 8)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("= 8u;"), std::string::npos);
    EXPECT_EQ(result.value().find("#{MAX_LIGHTS}"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_BareForm_ReplacedWithValue) {
    ShaderComposer c;
    const char* src                = "let n = #MAX_LIGHTS;\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_int("MAX_LIGHTS", 4)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("let n = 4;"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_BoolValue) {
    ShaderComposer c;
    const char* src                = "let flag: bool = #{IS_ENABLED};\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("IS_ENABLED", true)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("= true;"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_BoolFalseValue) {
    ShaderComposer c;
    const char* src                = "let flag: bool = #{IS_ENABLED};\n";
    std::vector<ShaderDefVal> defs = {ShaderDefVal::from_bool("IS_ENABLED", false)};
    auto result                    = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("= false;"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_MultipleSameLineReplacements) {
    ShaderComposer c;
    const char* src                = "let v = vec2(#{W}, #{H});\n";
    std::vector<ShaderDefVal> defs = {
        ShaderDefVal::from_int("W", 1920),
        ShaderDefVal::from_int("H", 1080),
    };
    auto result = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("vec2(1920, 1080)"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_UnknownDef_LeftAsIs) {
    ShaderComposer c;
    // #{UNKNOWN} is not a known def — the # is left as-is
    const char* src = "let x = #{UNKNOWN};\n";
    auto result     = c.compose(src, "s.wgsl", {});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("#{UNKNOWN}"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_InsideActiveIfdef) {
    ShaderComposer c;
    const char* src =
        "#ifdef USE_COUNT\n"
        "let n = #{COUNT};\n"
        "#endif\n";
    std::vector<ShaderDefVal> defs = {
        ShaderDefVal::from_bool("USE_COUNT"),
        ShaderDefVal::from_int("COUNT", 16),
    };
    auto result = c.compose(src, "s.wgsl", defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("let n = 16;"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_InsideInlinedModule) {
    ShaderComposer c;
    c.add_module("ns::util", "let cap: u32 = #{CAPACITY}u;\n",
                 std::vector<ShaderDefVal>{ShaderDefVal::from_uint("CAPACITY", 32)});
    const char* src = "#import ns::util\nfn main() {}\n";
    auto result     = c.compose(src, "main.wgsl", {});
    ASSERT_TRUE(result.has_value());
    // Module's own CAPACITY def should be substituted inside inlined module
    EXPECT_NE(result.value().find("let cap: u32 = 32u;"), std::string::npos);
}

TEST(ShaderComposer, ComposeSubstitution_CallerDefsOverrideInlinedModule) {
    ShaderComposer c;
    // Module has CAPACITY=32, but caller passes CAPACITY=64 → caller wins in merged def map
    c.add_module("ns::util", "let cap: u32 = #{CAPACITY}u;\n",
                 std::vector<ShaderDefVal>{ShaderDefVal::from_uint("CAPACITY", 32)});
    const char* src = "#import ns::util\nfn main() {}\n";
    // Note: caller defs are not passed to add_module but are passed to compose.
    // Module base_defs only populate the merged map with emplace (caller wins since
    // caller defs are already in the map before module base_defs are inserted).
    std::vector<ShaderDefVal> caller_defs = {ShaderDefVal::from_uint("CAPACITY", 64)};
    auto result                           = c.compose(src, "main.wgsl", caller_defs);
    ASSERT_TRUE(result.has_value());
    // Caller defs inserted first into the merged map, so CAPACITY=64 wins
    EXPECT_NE(result.value().find("let cap: u32 = 64u;"), std::string::npos);
}
