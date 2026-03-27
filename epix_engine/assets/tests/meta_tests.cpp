#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// AssetMetaCheck enum
// ===========================================================================

TEST(AssetMetaCheck, EnumValues) {
    EXPECT_NE(AssetMetaCheck::Always, AssetMetaCheck::Never);
    EXPECT_NE(AssetMetaCheck::Always, AssetMetaCheck::Paths);
    EXPECT_NE(AssetMetaCheck::Never, AssetMetaCheck::Paths);
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
    ProcessDependencyInfo info{.full_hash = 42, .path = "textures/a.png"};
    EXPECT_EQ(info.full_hash, 42u);
    EXPECT_EQ(info.path, "textures/a.png");
}

// ===========================================================================
// ProcessedInfo
// ===========================================================================

TEST(ProcessedInfo, DefaultValues) {
    ProcessedInfo info;
    EXPECT_EQ(info.hash, 0u);
    EXPECT_EQ(info.full_hash, 0u);
    EXPECT_TRUE(info.process_dependencies.empty());
}

TEST(ProcessedInfo, WithDependencies) {
    ProcessedInfo info;
    info.hash      = 100;
    info.full_hash = 200;
    info.process_dependencies.push_back({.full_hash = 50, .path = "dep.txt"});
    EXPECT_EQ(info.process_dependencies.size(), 1u);
    EXPECT_EQ(info.process_dependencies[0].full_hash, 50u);
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
    meta.processed = ProcessedInfo{.hash = 1, .full_hash = 2};
    ASSERT_NE(meta.processed_info(), nullptr);
    EXPECT_EQ(meta.processed_info()->hash, 1u);
    EXPECT_EQ(meta.processed_info()->full_hash, 2u);
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
