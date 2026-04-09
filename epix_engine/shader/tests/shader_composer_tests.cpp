#include <gtest/gtest.h>

import std;
import epix.shader;

using namespace epix::shader;

TEST(ShaderComposerWgsl, IfdefElseSelectsBranchFromDefinitions) {
    ShaderComposer composer;
    const std::array fast_defs = {ShaderDefVal::from_bool("USE_FAST")};
    const char* source =
        "#ifdef USE_FAST\n"
        "fn selected() -> i32 { return 1; }\n"
        "#else\n"
        "fn selected() -> i32 { return 2; }\n"
        "#endif\n";

    auto fast = composer.compose(source, "main.wgsl", fast_defs);
    ASSERT_TRUE(fast.has_value());
    EXPECT_NE(fast->find("return 1;"), std::string::npos);
    EXPECT_EQ(fast->find("return 2;"), std::string::npos);

    auto slow = composer.compose(source, "main.wgsl", {});
    ASSERT_TRUE(slow.has_value());
    EXPECT_NE(slow->find("return 2;"), std::string::npos);
    EXPECT_EQ(slow->find("return 1;"), std::string::npos);
}

TEST(ShaderComposerWgsl, ImportsInsideActiveConditionalAreInlined) {
    ShaderComposer composer;
    const std::array optional_defs = {ShaderDefVal::from_bool("USE_OPTIONAL")};
    ASSERT_TRUE(composer.add_module("math::optional", "fn optional_value() -> i32 { return 7; }\n", {}).has_value());

    const char* source =
        "#ifdef USE_OPTIONAL\n"
        "#import math::optional\n"
        "#endif\n"
        "fn main_value() -> i32 {\n"
        "#ifdef USE_OPTIONAL\n"
        "    return optional_value();\n"
        "#else\n"
        "    return 0;\n"
        "#endif\n"
        "}\n";

    auto with_import = composer.compose(source, "main.wgsl", optional_defs);
    ASSERT_TRUE(with_import.has_value());
    EXPECT_NE(with_import->find("fn optional_value()"), std::string::npos);
    EXPECT_NE(with_import->find("return optional_value();"), std::string::npos);

    auto without_import = composer.compose(source, "main.wgsl", {});
    ASSERT_TRUE(without_import.has_value());
    EXPECT_EQ(without_import->find("fn optional_value()"), std::string::npos);
    EXPECT_NE(without_import->find("return 0;"), std::string::npos);
}