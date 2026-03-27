#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// AssetSourceId
// ===========================================================================

TEST(AssetSourceId, DefaultIsDefault) {
    AssetSourceId id;
    EXPECT_TRUE(id.is_default());
    EXPECT_FALSE(id.as_str().has_value());
}

TEST(AssetSourceId, NamedSourceNotDefault) {
    AssetSourceId id(std::string("embedded"));
    EXPECT_FALSE(id.is_default());
    ASSERT_TRUE(id.as_str().has_value());
    EXPECT_EQ(id.as_str().value(), "embedded");
}

TEST(AssetSourceId, Equality) {
    AssetSourceId a(std::string("src1"));
    AssetSourceId b(std::string("src1"));
    AssetSourceId c(std::string("src2"));
    AssetSourceId d;
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_EQ(d, AssetSourceId{});
}

// ===========================================================================
// AssetPath — Construction
// ===========================================================================

TEST(AssetPath, DefaultConstruction) {
    AssetPath p;
    EXPECT_TRUE(p.source.is_default());
    EXPECT_TRUE(p.path.empty());
    EXPECT_FALSE(p.label.has_value());
}

TEST(AssetPath, FromSimpleString) {
    AssetPath p("textures/hero.png");
    EXPECT_TRUE(p.source.is_default());
    EXPECT_EQ(p.path.string(), "textures\\hero.png");  // Win normalised
    EXPECT_FALSE(p.label.has_value());
}

TEST(AssetPath, FromStringWithSource) {
    AssetPath p("embedded://models/cube.obj");
    ASSERT_TRUE(p.source.as_str().has_value());
    EXPECT_EQ(p.source.as_str().value(), "embedded");
    EXPECT_EQ(p.path.filename().string(), "cube.obj");
    EXPECT_FALSE(p.label.has_value());
}

TEST(AssetPath, FromStringWithLabel) {
    AssetPath p("scene.gltf#Mesh0");
    EXPECT_TRUE(p.source.is_default());
    EXPECT_EQ(p.path.filename().string(), "scene.gltf");
    ASSERT_TRUE(p.label.has_value());
    EXPECT_EQ(p.label.value(), "Mesh0");
}

TEST(AssetPath, FromStringWithSourceAndLabel) {
    AssetPath p("remote://assets/scene.gltf#Light0");
    ASSERT_TRUE(p.source.as_str().has_value());
    EXPECT_EQ(p.source.as_str().value(), "remote");
    ASSERT_TRUE(p.label.has_value());
    EXPECT_EQ(p.label.value(), "Light0");
}

TEST(AssetPath, ComponentConstruction) {
    AssetPath p(AssetSourceId(std::string("src")), "dir/file.txt", std::string("lbl"));
    EXPECT_EQ(p.source.as_str().value(), "src");
    EXPECT_EQ(p.path.filename().string(), "file.txt");
    EXPECT_EQ(p.label.value(), "lbl");
}

// ===========================================================================
// AssetPath — string()
// ===========================================================================

TEST(AssetPath, ToString_Simple) {
    AssetPath p("textures/hero.png");
    auto s = p.string();
    EXPECT_NE(s.find("hero.png"), std::string::npos);
    // Should not contain "://"
    EXPECT_EQ(s.find("://"), std::string::npos);
    // Should not contain "#"
    EXPECT_EQ(s.find('#'), std::string::npos);
}

TEST(AssetPath, ToString_WithSource) {
    AssetPath p("embedded://hero.png");
    auto s = p.string();
    EXPECT_NE(s.find("embedded://"), std::string::npos);
}

TEST(AssetPath, ToString_WithLabel) {
    AssetPath p("scene.gltf#Mesh0");
    auto s = p.string();
    EXPECT_NE(s.find("#Mesh0"), std::string::npos);
}

// ===========================================================================
// AssetPath — with_* / without_* / remove_label / take_label
// ===========================================================================

TEST(AssetPath, WithLabel) {
    AssetPath base("model.gltf");
    auto labeled = base.with_label("Mesh0");
    EXPECT_EQ(base.path, labeled.path);
    EXPECT_FALSE(base.label.has_value());
    ASSERT_TRUE(labeled.label.has_value());
    EXPECT_EQ(labeled.label.value(), "Mesh0");
}

TEST(AssetPath, WithSource) {
    AssetPath base("model.gltf");
    auto sourced = base.with_source(AssetSourceId(std::string("embedded")));
    EXPECT_TRUE(base.source.is_default());
    ASSERT_TRUE(sourced.source.as_str().has_value());
    EXPECT_EQ(sourced.source.as_str().value(), "embedded");
}

TEST(AssetPath, WithoutLabel) {
    AssetPath p("scene.gltf#Mesh0");
    auto stripped = p.without_label();
    EXPECT_FALSE(stripped.label.has_value());
    EXPECT_EQ(stripped.path, p.path);
}

TEST(AssetPath, RemoveLabel) {
    AssetPath p("scene.gltf#Mesh0");
    ASSERT_TRUE(p.label.has_value());
    p.remove_label();
    EXPECT_FALSE(p.label.has_value());
}

TEST(AssetPath, TakeLabel) {
    AssetPath p("scene.gltf#Mesh0");
    auto taken = p.take_label();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken.value(), "Mesh0");
    EXPECT_FALSE(p.label.has_value());
}

TEST(AssetPath, TakeLabel_NoLabel) {
    AssetPath p("scene.gltf");
    auto taken = p.take_label();
    EXPECT_FALSE(taken.has_value());
}

// ===========================================================================
// AssetPath — parent / resolve
// ===========================================================================

TEST(AssetPath, Parent_ReturnsParentDir) {
    AssetPath p("a/b/c.txt");
    auto par = p.parent();
    ASSERT_TRUE(par.has_value());
    EXPECT_NE(par->path.string().find("b"), std::string::npos);
}

TEST(AssetPath, Parent_NoParent) {
    AssetPath p("file.txt");
    auto par = p.parent();
    EXPECT_FALSE(par.has_value());
}

TEST(AssetPath, Resolve_Relative) {
    AssetPath base("textures/hero.png");
    AssetPath relative("enemy.png");
    auto resolved = base.resolve(relative);
    EXPECT_NE(resolved.path.string().find("enemy.png"), std::string::npos);
    EXPECT_NE(resolved.path.string().find("textures"), std::string::npos);
}

TEST(AssetPath, Resolve_StringView) {
    AssetPath base("textures/hero.png");
    auto resolved = base.resolve("../sounds/boom.wav");
    EXPECT_NE(resolved.path.string().find("boom.wav"), std::string::npos);
}

// ===========================================================================
// AssetPath — extensions
// ===========================================================================

TEST(AssetPath, GetExtension_Simple) {
    AssetPath p("model.obj");
    auto ext = p.get_extension();
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(ext.value(), "obj");
}

TEST(AssetPath, GetExtension_Double) {
    AssetPath p("scene.gltf.json");
    auto ext = p.get_extension();
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(ext.value(), "json");
}

TEST(AssetPath, GetExtension_None) {
    AssetPath p("Makefile");
    auto ext = p.get_extension();
    EXPECT_FALSE(ext.has_value());
}

TEST(AssetPath, GetFullExtension_Double) {
    AssetPath p("scene.gltf.json");
    auto ext = p.get_full_extension();
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(ext.value(), "gltf.json");
}

TEST(AssetPath, GetFullExtension_Single) {
    AssetPath p("model.obj");
    auto ext = p.get_full_extension();
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(ext.value(), "obj");
}

TEST(AssetPath, GetFullExtension_None) {
    AssetPath p("Makefile");
    auto ext = p.get_full_extension();
    EXPECT_FALSE(ext.has_value());
}

TEST(AssetPath, IterSecondaryExtensions_Double) {
    AssetPath p("model.gltf.json");
    auto sec = p.iter_secondary_extensions();
    ASSERT_EQ(sec.size(), 1u);
    EXPECT_EQ(sec[0], "gltf");
}

TEST(AssetPath, IterSecondaryExtensions_Triple) {
    AssetPath p("data.a.b.c");
    auto sec = p.iter_secondary_extensions();
    ASSERT_EQ(sec.size(), 2u);
    EXPECT_EQ(sec[0], "a");
    EXPECT_EQ(sec[1], "b");
}

TEST(AssetPath, IterSecondaryExtensions_Single) {
    AssetPath p("model.obj");
    auto sec = p.iter_secondary_extensions();
    EXPECT_TRUE(sec.empty());
}

TEST(AssetPath, IterSecondaryExtensions_None) {
    AssetPath p("Makefile");
    auto sec = p.iter_secondary_extensions();
    EXPECT_TRUE(sec.empty());
}

// ===========================================================================
// AssetPath — try_parse
// ===========================================================================

TEST(AssetPath, TryParse_Valid) {
    auto opt = AssetPath::try_parse("textures/hero.png");
    ASSERT_TRUE(opt.has_value());
    EXPECT_NE(opt->path.string().find("hero.png"), std::string::npos);
}

TEST(AssetPath, TryParse_Empty) {
    auto opt = AssetPath::try_parse("");
    EXPECT_FALSE(opt.has_value());
}

TEST(AssetPath, TryParse_WithSourceAndLabel) {
    auto opt = AssetPath::try_parse("embedded://model.gltf#Mesh0");
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->source.as_str().value(), "embedded");
    EXPECT_EQ(opt->label.value(), "Mesh0");
}

// ===========================================================================
// AssetPath — equality & ordering
// ===========================================================================

TEST(AssetPath, Equality) {
    AssetPath a("textures/hero.png");
    AssetPath b("textures/hero.png");
    EXPECT_EQ(a, b);
}

TEST(AssetPath, Inequality_DifferentLabel) {
    AssetPath a("scene.gltf#Mesh0");
    AssetPath b("scene.gltf#Mesh1");
    EXPECT_NE(a, b);
}

TEST(AssetPath, Inequality_DifferentSource) {
    AssetPath a("embedded://f.txt");
    AssetPath b("f.txt");
    EXPECT_NE(a, b);
}

// ===========================================================================
// AssetPath — hashing
// ===========================================================================

TEST(AssetPath, Hash_SamePathSameHash) {
    AssetPath a("textures/hero.png");
    AssetPath b("textures/hero.png");
    EXPECT_EQ(std::hash<AssetPath>{}(a), std::hash<AssetPath>{}(b));
}

TEST(AssetPath, Hash_DifferentPathLikelyDifferent) {
    AssetPath a("textures/hero.png");
    AssetPath b("textures/enemy.png");
    // Not guaranteed but highly likely
    EXPECT_NE(std::hash<AssetPath>{}(a), std::hash<AssetPath>{}(b));
}
