#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace epix::assets;

// ===========================================================================
// AssetMetaCheck variant
// ===========================================================================

TEST(AssetMetaCheck, DistinctVariants) {
    AssetMetaCheck always{asset_meta_check::Always{}};
    AssetMetaCheck never{asset_meta_check::Never{}};
    AssetMetaCheck paths{asset_meta_check::Paths{}};
    EXPECT_TRUE(std::holds_alternative<asset_meta_check::Always>(always));
    EXPECT_TRUE(std::holds_alternative<asset_meta_check::Never>(never));
    EXPECT_TRUE(std::holds_alternative<asset_meta_check::Paths>(paths));
    EXPECT_FALSE(std::holds_alternative<asset_meta_check::Always>(never));
    EXPECT_FALSE(std::holds_alternative<asset_meta_check::Always>(paths));
    EXPECT_FALSE(std::holds_alternative<asset_meta_check::Never>(paths));
}

// ===========================================================================
// UnapprovedPathMode enum
// ===========================================================================

TEST(UnapprovedPathMode, EnumValues) {
    EXPECT_NE(UnapprovedPathMode::Allow, UnapprovedPathMode::Deny);
    EXPECT_NE(UnapprovedPathMode::Allow, UnapprovedPathMode::Forbid);
    EXPECT_NE(UnapprovedPathMode::Deny, UnapprovedPathMode::Forbid);
}

// ===========================================================================
// AssetActionType enum
// ===========================================================================

TEST(AssetActionType, EnumValues) {
    EXPECT_NE(AssetActionType::Load, AssetActionType::Process);
    EXPECT_NE(AssetActionType::Load, AssetActionType::Ignore);
    EXPECT_NE(AssetActionType::Process, AssetActionType::Ignore);
}

// ===========================================================================
// ProcessDependencyInfo
// ===========================================================================

TEST(ProcessDependencyInfo, Construction) {
    AssetHash h = {};
    h[0]        = 42;
    ProcessDependencyInfo info{.full_hash = h, .path = "textures/a.png"};
    EXPECT_TRUE(info.full_hash == h);
    EXPECT_EQ(info.path, "textures/a.png");
}

// ===========================================================================
// ProcessedInfo
// ===========================================================================

TEST(ProcessedInfo, DefaultValues) {
    ProcessedInfo info;
    EXPECT_TRUE(info.hash == AssetHash{});
    EXPECT_TRUE(info.full_hash == AssetHash{});
    EXPECT_TRUE(info.process_dependencies.empty());
}

TEST(ProcessedInfo, WithDependencies) {
    ProcessedInfo info;
    AssetHash h100 = {};
    h100[0]        = 100;
    AssetHash h200 = {};
    h200[0]        = 200;
    AssetHash h50  = {};
    h50[0]         = 50;
    info.hash      = h100;
    info.full_hash = h200;
    info.process_dependencies.push_back({.full_hash = h50, .path = "dep.txt"});
    EXPECT_EQ(info.process_dependencies.size(), 1u);
    EXPECT_TRUE(info.process_dependencies[0].full_hash == h50);
}

// ===========================================================================
// AssetAction — Load variant
// ===========================================================================

struct TestLoaderSettings {
    int quality = 5;
};
struct TestProcessSettings {
    bool optimize = false;
};

TEST(AssetAction, DefaultIsLoad) {
    AssetAction<TestLoaderSettings, TestProcessSettings> action;
    EXPECT_EQ(action.type(), AssetActionType::Load);
}

TEST(AssetAction, LoadVariant) {
    using Action = AssetAction<TestLoaderSettings, TestProcessSettings>;
    Action a;
    a.inner = typename Action::Load{.settings = {.quality = 10}};
    EXPECT_EQ(a.type(), AssetActionType::Load);
    auto& load = std::get<typename Action::Load>(a.inner);
    EXPECT_EQ(load.settings.quality, 10);
}

TEST(AssetAction, ProcessVariant) {
    using Action = AssetAction<TestLoaderSettings, TestProcessSettings>;
    Action a;
    a.inner = typename Action::Process{.settings = {.optimize = true}, .processor = "MyProcessor"};
    EXPECT_EQ(a.type(), AssetActionType::Process);
    auto& proc = std::get<typename Action::Process>(a.inner);
    EXPECT_TRUE(proc.settings.optimize);
    EXPECT_EQ(proc.processor, "MyProcessor");
}

TEST(AssetAction, IgnoreVariant) {
    using Action = AssetAction<TestLoaderSettings, TestProcessSettings>;
    Action a;
    a.inner = typename Action::Ignore{};
    EXPECT_EQ(a.type(), AssetActionType::Ignore);
}

// ===========================================================================
// AssetMeta — concrete metadata
// ===========================================================================

TEST(AssetMeta, DefaultValues) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    EXPECT_EQ(meta.meta_format_version, std::string(META_FORMAT_VERSION));
    EXPECT_EQ(meta.action, AssetActionType::Load);
    EXPECT_FALSE(meta.processed.has_value());
}

TEST(AssetMeta, LoaderName_WhenLoad) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "MyLoader";
    auto name   = meta.loader_name();
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "MyLoader");
}

TEST(AssetMeta, LoaderName_WhenProcess) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Process;
    meta.loader = "MyLoader";
    EXPECT_FALSE(meta.loader_name().has_value());
}

TEST(AssetMeta, ProcessorName_WhenProcess) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action    = AssetActionType::Process;
    meta.processor = "MyProc";
    auto name      = meta.processor_name();
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "MyProc");
}

TEST(AssetMeta, ProcessorName_WhenLoad) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action    = AssetActionType::Load;
    meta.processor = "MyProc";
    EXPECT_FALSE(meta.processor_name().has_value());
}

TEST(AssetMeta, ProcessedInfo_Null) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    EXPECT_EQ(meta.processed_info(), nullptr);
}

TEST(AssetMeta, ProcessedInfo_Present) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    AssetHash h1   = {};
    h1[0]          = 1;
    AssetHash h2   = {};
    h2[0]          = 2;
    meta.processed = ProcessedInfo{.hash = h1, .full_hash = h2};
    ASSERT_NE(meta.processed_info(), nullptr);
    EXPECT_TRUE(meta.processed_info()->hash == h1);
    EXPECT_TRUE(meta.processed_info()->full_hash == h2);
}

TEST(AssetMeta, ActionType) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Ignore;
    EXPECT_EQ(meta.action_type(), AssetActionType::Ignore);
}

// ===========================================================================
// AssetMetaDyn — virtual interface
// ===========================================================================

TEST(AssetMetaDyn, PolymorphicAccess) {
    auto meta    = std::make_unique<AssetMeta<TestLoaderSettings, TestProcessSettings>>();
    meta->action = AssetActionType::Load;
    meta->loader = "TestLoader";

    AssetMetaDyn* dyn = meta.get();
    EXPECT_EQ(dyn->action_type(), AssetActionType::Load);
    auto name = dyn->loader_name();
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "TestLoader");
    EXPECT_FALSE(dyn->processor_name().has_value());
    EXPECT_EQ(dyn->processed_info(), nullptr);
}

// ===========================================================================
// Settings base class
// ===========================================================================

struct MySettings : Settings {
    int quality    = 5;
    bool grayscale = false;
};

struct MyOtherSettings : Settings {
    float scale = 1.0f;
};

TEST(SettingsBase, DerivedIsSettings) {
    MySettings s;
    Settings* base = &s;
    EXPECT_NE(dynamic_cast<MySettings*>(base), nullptr);
}

TEST(SettingsBase, DynamicCast_WrongType_ReturnsNull) {
    MySettings s;
    Settings* base = &s;
    EXPECT_EQ(dynamic_cast<MyOtherSettings*>(base), nullptr);
}

// ===========================================================================
// AssetMeta::loader_settings() — Settings-derived LoaderSettings
// ===========================================================================

struct DerivedLoaderSettings : Settings {
    int level = 3;
};
struct DerivedProcessSettings : Settings {
    bool compress = true;
};

TEST(AssetMeta, LoaderSettings_WhenLoad_ReturnsPointer) {
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action                      = AssetActionType::Load;
    meta.loader_settings_value.level = 42;

    Settings* s = meta.loader_settings();
    ASSERT_NE(s, nullptr);
    auto* concrete = dynamic_cast<DerivedLoaderSettings*>(s);
    ASSERT_NE(concrete, nullptr);
    EXPECT_EQ(concrete->level, 42);
}

TEST(AssetMeta, LoaderSettings_WhenProcess_ReturnsNull) {
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action = AssetActionType::Process;
    EXPECT_EQ(meta.loader_settings(), nullptr);
}

TEST(AssetMeta, LoaderSettings_WhenIgnore_ReturnsNull) {
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action = AssetActionType::Ignore;
    EXPECT_EQ(meta.loader_settings(), nullptr);
}

TEST(AssetMeta, LoaderSettings_Const_WhenLoad_ReturnsPointer) {
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action                      = AssetActionType::Load;
    meta.loader_settings_value.level = 99;

    const AssetMetaDyn& dyn = meta;
    const Settings* s       = dyn.loader_settings();
    ASSERT_NE(s, nullptr);
    auto* concrete = dynamic_cast<const DerivedLoaderSettings*>(s);
    ASSERT_NE(concrete, nullptr);
    EXPECT_EQ(concrete->level, 99);
}

TEST(AssetMeta, LoaderSettings_NonDerived_ReturnsNull) {
    // TestLoaderSettings does NOT derive from Settings — should always return nullptr
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    EXPECT_EQ(meta.loader_settings(), nullptr);

    const AssetMetaDyn& dyn = meta;
    EXPECT_EQ(dyn.loader_settings(), nullptr);
}

// ===========================================================================
// MetaTransform type
// ===========================================================================

TEST(MetaTransform, CanMutateActionType) {
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action = AssetActionType::Load;

    MetaTransform transform = [](AssetMetaDyn& m) {
        // We can't change action_type via the dyn interface, but we can verify
        // calling loader_settings works inside a transform.
        auto* s = m.loader_settings();
        if (s) {
            if (auto* ds = dynamic_cast<DerivedLoaderSettings*>(s)) {
                ds->level = 100;
            }
        }
    };

    transform(meta);
    EXPECT_EQ(meta.loader_settings_value.level, 100);
}

TEST(MetaTransform, IsCallable) {
    bool called             = false;
    MetaTransform transform = [&called](AssetMetaDyn&) { called = true; };
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    transform(meta);
    EXPECT_TRUE(called);
}

// ===========================================================================
// loader_settings_meta_transform
// ===========================================================================

TEST(LoaderSettingsMetaTransform, AppliesMutation) {
    auto mt = loader_settings_meta_transform<DerivedLoaderSettings>([](DerivedLoaderSettings& s) { s.level = 77; });

    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action                      = AssetActionType::Load;
    meta.loader_settings_value.level = 0;

    mt(meta);
    EXPECT_EQ(meta.loader_settings_value.level, 77);
}

TEST(LoaderSettingsMetaTransform, NoOpWhenNotLoad) {
    auto mt = loader_settings_meta_transform<DerivedLoaderSettings>([](DerivedLoaderSettings& s) { s.level = 77; });

    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action                      = AssetActionType::Process;
    meta.loader_settings_value.level = 0;

    mt(meta);
    EXPECT_EQ(meta.loader_settings_value.level, 0) << "Should not mutate when action != Load";
}

TEST(LoaderSettingsMetaTransform, WrongSettingsType_DoesNotCrash) {
    // Create a transform expecting MySettings, but apply to DerivedLoaderSettings meta
    auto mt = loader_settings_meta_transform<MySettings>([](MySettings& s) { s.quality = 999; });

    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action = AssetActionType::Load;

    // dynamic_cast should fail, but not crash — just logs an error
    EXPECT_NO_THROW(mt(meta));
    // The DerivedLoaderSettings should be unchanged
    EXPECT_EQ(meta.loader_settings_value.level, 3);
}

TEST(LoaderSettingsMetaTransform, MultipleMutations) {
    auto mt = loader_settings_meta_transform<MySettings>([](MySettings& s) {
        s.quality   = 10;
        s.grayscale = true;
    });

    AssetMeta<MySettings, DerivedProcessSettings> meta;
    meta.action = AssetActionType::Load;

    mt(meta);
    EXPECT_EQ(meta.loader_settings_value.quality, 10);
    EXPECT_TRUE(meta.loader_settings_value.grayscale);
}
