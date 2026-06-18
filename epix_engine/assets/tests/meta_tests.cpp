#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <typeinfo>
#include <variant>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
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

struct MySettings {
    int quality    = 5;
    bool grayscale = false;
};

struct MyOtherSettings {
    float scale = 1.0f;
};

TEST(SettingsBase, TryCast_CorrectType_ReturnsValue) {
    AssetMeta<MySettings, EmptySettings> meta;
    meta.loader_settings_storage.value.quality = 10;
    Settings* base                             = meta.loader_settings();
    ASSERT_NE(base, nullptr);
    auto result = base->try_cast<MySettings>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get().quality, 10);
}

TEST(SettingsBase, TryCast_WrongType_ReturnsNullopt) {
    AssetMeta<MySettings, EmptySettings> meta;
    Settings* base = meta.loader_settings();
    ASSERT_NE(base, nullptr);
    auto result = base->try_cast<MyOtherSettings>();
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// AssetMeta::loader_settings() — Settings-derived LoaderSettings
// ===========================================================================

struct DerivedLoaderSettings {
    int level = 3;
};
struct DerivedProcessSettings {
    bool compress = true;
};

TEST(AssetMeta, LoaderSettings_WhenLoad_ReturnsPointer) {
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    meta.action                              = AssetActionType::Load;
    meta.loader_settings_storage.value.level = 42;

    Settings* s = meta.loader_settings();
    ASSERT_NE(s, nullptr);
    auto result = s->try_cast<DerivedLoaderSettings>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get().level, 42);
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
    meta.action                              = AssetActionType::Load;
    meta.loader_settings_storage.value.level = 99;

    const AssetMetaDyn& dyn = meta;
    const Settings* s       = dyn.loader_settings();
    ASSERT_NE(s, nullptr);
    auto result = s->try_cast<DerivedLoaderSettings>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get().level, 99);
}

TEST(AssetMeta, LoaderSettings_WhenLoad_AlwaysReturnsStorage) {
    // loader_settings() returns &loader_settings_storage regardless of T
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    EXPECT_NE(meta.loader_settings(), nullptr);

    const AssetMetaDyn& dyn = meta;
    EXPECT_NE(dyn.loader_settings(), nullptr);
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
            if (auto ref = s->try_cast<DerivedLoaderSettings>()) {
                ref->get().level = 100;
            }
        }
    };

    transform(meta);
    EXPECT_EQ(meta.loader_settings_storage.value.level, 100);
}

TEST(MetaTransform, IsCallable) {
    bool called             = false;
    MetaTransform transform = [&called](AssetMetaDyn&) { called = true; };
    AssetMeta<DerivedLoaderSettings, DerivedProcessSettings> meta;
    transform(meta);
    EXPECT_TRUE(called);
}

// ===========================================================================
// loader_settings_meta_transform — internal helper; tested via
// AssetServer::load_with_settings integration tests in asset_server_plugin_tests.
// ===========================================================================

// ===========================================================================
// serialize_meta_minimal / deserialize_meta_minimal — round-trip tests
// ===========================================================================

TEST(SerializeMetaMinimal, RoundTrip_LoadAction) {
    AssetMetaMinimal original;
    original.meta_format_version = std::string(META_FORMAT_VERSION);
    original.asset.action        = AssetActionType::Load;
    original.asset.loader        = "epix_engine::TextLoader";
    original.asset.processor     = "";

    auto bytes = serialize_meta_minimal(original);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_FALSE(bytes->empty());

    auto result = deserialize_meta_minimal(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->meta_format_version, original.meta_format_version);
    EXPECT_EQ(result->asset.action, AssetActionType::Load);
    EXPECT_EQ(result->asset.loader, "epix_engine::TextLoader");
    EXPECT_EQ(result->asset.processor, "");
}

TEST(SerializeMetaMinimal, RoundTrip_ProcessAction) {
    AssetMetaMinimal original;
    original.meta_format_version = std::string(META_FORMAT_VERSION);
    original.asset.action        = AssetActionType::Process;
    original.asset.loader        = "";
    original.asset.processor     = "epix_engine::CompressProcessor";

    auto bytes = serialize_meta_minimal(original);
    ASSERT_TRUE(bytes.has_value());

    auto result = deserialize_meta_minimal(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->asset.action, AssetActionType::Process);
    EXPECT_EQ(result->asset.processor, "epix_engine::CompressProcessor");
    EXPECT_EQ(result->asset.loader, "");
}

TEST(SerializeMetaMinimal, RoundTrip_IgnoreAction) {
    AssetMetaMinimal original;
    original.meta_format_version = std::string(META_FORMAT_VERSION);
    original.asset.action        = AssetActionType::Ignore;
    original.asset.loader        = "";
    original.asset.processor     = "";

    auto bytes = serialize_meta_minimal(original);
    ASSERT_TRUE(bytes.has_value());

    auto result = deserialize_meta_minimal(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->asset.action, AssetActionType::Ignore);
    EXPECT_EQ(result->asset.loader, "");
    EXPECT_EQ(result->asset.processor, "");
}

TEST(SerializeMetaMinimal, SpanAndVectorOverloads_ProduceIdenticalResult) {
    AssetMetaMinimal original;
    original.meta_format_version = std::string(META_FORMAT_VERSION);
    original.asset.action        = AssetActionType::Load;
    original.asset.loader        = "SomeLoader";

    auto vec_bytes = serialize_meta_minimal(original);
    ASSERT_TRUE(vec_bytes.has_value());

    auto from_vec  = deserialize_meta_minimal(*vec_bytes);
    auto from_span = deserialize_meta_minimal(std::span<const std::byte>(vec_bytes->data(), vec_bytes->size()));
    ASSERT_TRUE(from_vec.has_value());
    ASSERT_TRUE(from_span.has_value());
    EXPECT_EQ(from_vec->asset.loader, from_span->asset.loader);
    EXPECT_EQ(from_vec->asset.action, from_span->asset.action);
}

TEST(SerializeMetaMinimal, EmptyBytes_ReturnsError) {
    std::vector<std::byte> empty{};
    auto result = deserialize_meta_minimal(empty);
    EXPECT_FALSE(result.has_value());
}

TEST(SerializeMetaMinimal, TruncatedBytes_ReturnsError) {
    AssetMetaMinimal original;
    original.meta_format_version = std::string(META_FORMAT_VERSION);
    original.asset.loader        = "SomeLoader";

    auto bytes = serialize_meta_minimal(original);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GT(bytes->size(), 2u);

    auto truncated = std::vector<std::byte>(bytes->begin(), bytes->begin() + 2);
    auto result    = deserialize_meta_minimal(truncated);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// serialize_asset_meta / deserialize_asset_meta — round-trip tests
// ===========================================================================

struct SerLoadSettings {
    int quality = 5;
    float scale = 1.0f;
};

struct SerProcSettings {
    bool compress = false;
    int level     = 0;
};

TEST(SerializeAssetMeta, RoundTrip_EmptySettings) {
    AssetMeta<EmptySettings> meta;
    meta.meta_format_version = std::string(META_FORMAT_VERSION);
    meta.action              = AssetActionType::Load;
    meta.loader              = "TestLoader";

    auto bytes = serialize_asset_meta(meta);
    ASSERT_TRUE(bytes.has_value());

    auto result = deserialize_asset_meta<EmptySettings, EmptySettings>(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->loader, "TestLoader");
    EXPECT_EQ(result->action, AssetActionType::Load);
    EXPECT_EQ(result->meta_format_version, std::string(META_FORMAT_VERSION));
}

TEST(SerializeAssetMeta, RoundTrip_WithSettings) {
    AssetMeta<SerLoadSettings, SerProcSettings> meta;
    meta.meta_format_version                       = std::string(META_FORMAT_VERSION);
    meta.action                                    = AssetActionType::Load;
    meta.loader                                    = "MyLoader";
    meta.loader_settings_storage.value.quality     = 10;
    meta.loader_settings_storage.value.scale       = 2.5f;
    meta.processor                                 = "";
    meta.processor_settings_storage.value.compress = true;
    meta.processor_settings_storage.value.level    = 7;

    auto bytes = serialize_asset_meta(meta);
    ASSERT_TRUE(bytes.has_value());

    auto result = deserialize_asset_meta<SerLoadSettings, SerProcSettings>(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->loader, "MyLoader");
    EXPECT_EQ(result->action, AssetActionType::Load);
    EXPECT_EQ(result->loader_settings_storage.value.quality, 10);
    EXPECT_FLOAT_EQ(result->loader_settings_storage.value.scale, 2.5f);
    EXPECT_TRUE(result->processor_settings_storage.value.compress);
    EXPECT_EQ(result->processor_settings_storage.value.level, 7);
}

TEST(SerializeAssetMeta, RoundTrip_WithProcessedInfo) {
    AssetMeta<SerLoadSettings, SerProcSettings> meta;
    meta.meta_format_version = std::string(META_FORMAT_VERSION);
    meta.action              = AssetActionType::Process;
    meta.processor           = "MyProc";

    AssetHash h1{};
    h1[0] = 1;
    h1[1] = 2;
    AssetHash h2{};
    h2[31]                = 255;
    std::int64_t mtime_ns = 123456789;

    ProcessDependencyInfo dep;
    dep.full_hash = h1;
    dep.path      = "dep/texture.png";
    meta.processed =
        ProcessedInfo{.hash = h1, .full_hash = h2, .source_mtime_ns = mtime_ns, .process_dependencies = {dep}};

    auto bytes = serialize_asset_meta(meta);
    ASSERT_TRUE(bytes.has_value());

    auto result = deserialize_asset_meta<SerLoadSettings, SerProcSettings>(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->action, AssetActionType::Process);
    EXPECT_EQ(result->processor, "MyProc");
    ASSERT_TRUE(result->processed.has_value());
    EXPECT_TRUE(result->processed->hash == h1);
    EXPECT_TRUE(result->processed->full_hash == h2);
    ASSERT_TRUE(result->processed->source_mtime_ns.has_value());
    EXPECT_EQ(*result->processed->source_mtime_ns, mtime_ns);
    ASSERT_EQ(result->processed->process_dependencies.size(), 1u);
    EXPECT_EQ(result->processed->process_dependencies[0].path, "dep/texture.png");
    EXPECT_TRUE(result->processed->process_dependencies[0].full_hash == h1);
}

// ===========================================================================
// settings_cast<T> tests
// ===========================================================================

TEST(SettingsCast, Const_ReturnsValue) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.loader_settings_storage.value.quality = 42;
    const Settings& base                       = *meta.loader_settings();
    EXPECT_EQ(base.cast<TestLoaderSettings>().quality, 42);
}

TEST(SettingsCast, NonConst_MutatesValue) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.loader_settings_storage.value.quality = 1;
    Settings& base                             = *meta.loader_settings();
    base.cast<TestLoaderSettings>().quality    = 99;
    EXPECT_EQ(meta.loader_settings_storage.value.quality, 99);
}

TEST(SettingsCast, WrongType_ThrowsBadCast) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    Settings& base = *meta.loader_settings();
    EXPECT_THROW(base.cast<TestProcessSettings>(), std::bad_cast);
}

TEST(SettingsCast, WrongType_Const_ThrowsBadCast) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    const Settings& base = *meta.loader_settings();
    EXPECT_THROW(base.cast<TestProcessSettings>(), std::bad_cast);
}

// ===========================================================================
// AssetMetaDyn::processed_info_mut — new mutable accessor
// ===========================================================================

TEST(AssetMetaDyn, ProcessedInfoMut_SetViaRef) {
    auto meta = std::make_unique<AssetMeta<TestLoaderSettings, TestProcessSettings>>();
    EXPECT_FALSE(meta->processed.has_value());

    AssetMetaDyn* dyn = meta.get();
    AssetHash h{};
    h[0]                      = 7;
    dyn->processed_info_mut() = ProcessedInfo{.hash = h};

    ASSERT_TRUE(meta->processed.has_value());
    EXPECT_TRUE(meta->processed->hash == h);
}

TEST(AssetMetaDyn, ProcessedInfoMut_ClearsViaOptional) {
    auto meta = std::make_unique<AssetMeta<TestLoaderSettings, TestProcessSettings>>();
    AssetHash h{};
    h[0]            = 1;
    meta->processed = ProcessedInfo{.hash = h};
    EXPECT_TRUE(meta->processed.has_value());

    AssetMetaDyn* dyn         = meta.get();
    dyn->processed_info_mut() = std::nullopt;
    EXPECT_FALSE(meta->processed.has_value());
}

TEST(AssetMetaDyn, ProcessedInfoMut_SameObjectAsField) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    // The reference returned must alias meta.processed
    EXPECT_EQ(&meta.processed_info_mut(), &meta.processed);
}

// ===========================================================================
// AssetMetaDyn::process_settings — const and non-const
// ===========================================================================

TEST(AssetMetaDyn, ProcessSettings_WhenProcess_ReturnsNonNull) {
    using Meta = AssetMeta<TestLoaderSettings, TestProcessSettings>;
    Meta meta;
    meta.action = AssetActionType::Process;

    Settings* s = meta.process_settings();
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->try_cast<TestProcessSettings>().has_value());
}

TEST(AssetMetaDyn, ProcessSettings_WhenLoad_ReturnsNull) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    EXPECT_EQ(meta.process_settings(), nullptr);
}

TEST(AssetMetaDyn, ProcessSettings_WhenIgnore_ReturnsNull) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Ignore;
    EXPECT_EQ(meta.process_settings(), nullptr);
}

TEST(AssetMetaDyn, ProcessSettings_Const_WhenProcess_ReturnsNonNull) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action             = AssetActionType::Process;
    const AssetMetaDyn& dyn = meta;
    const Settings* s       = dyn.process_settings();
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->try_cast<TestProcessSettings>().has_value());
}

TEST(AssetMetaDyn, ProcessSettings_Const_WhenLoad_ReturnsNull) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action             = AssetActionType::Load;
    const AssetMetaDyn& dyn = meta;
    EXPECT_EQ(dyn.process_settings(), nullptr);
}

TEST(AssetMetaDyn, ProcessSettings_VirtualDispatch) {
    // Set via the concrete type and read via the abstract interface
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action                                    = AssetActionType::Process;
    meta.processor_settings_storage.value.optimize = true;

    AssetMetaDyn* dyn = &meta;
    Settings* s       = dyn->process_settings();
    ASSERT_NE(s, nullptr);
    auto result = s->try_cast<TestProcessSettings>();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->get().optimize);
}

// ===========================================================================
// AssetMetaDyn::serialize_bytes — virtual round-trip
// ===========================================================================

TEST(AssetMetaDyn, SerializeBytes_ProducesNonEmptyVector) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "SomeLoader";

    AssetMetaDyn* dyn = &meta;
    auto bytes        = dyn->serialize_bytes();
    EXPECT_FALSE(bytes.empty());
}

TEST(AssetMetaDyn, SerializeBytes_MatchesSerializeAssetMeta) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "MatchLoader";

    auto via_free = serialize_asset_meta(meta);
    ASSERT_TRUE(via_free.has_value());

    auto via_dyn = meta.serialize_bytes();
    EXPECT_EQ(*via_free, via_dyn);
}

TEST(AssetMetaDyn, SerializeBytes_RoundTrip_ThroughDynamic) {
    AssetMeta<SerLoadSettings, SerProcSettings> original;
    original.action                                = AssetActionType::Load;
    original.loader                                = "DynLoader";
    original.loader_settings_storage.value.quality = 33;
    original.loader_settings_storage.value.scale   = 0.5f;

    AssetMetaDyn* dyn = &original;
    auto bytes        = dyn->serialize_bytes();
    ASSERT_FALSE(bytes.empty());

    auto result = deserialize_asset_meta<SerLoadSettings, SerProcSettings>(
        std::span<const std::byte>(bytes.data(), bytes.size()));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->loader, "DynLoader");
    EXPECT_EQ(result->loader_settings_storage.value.quality, 33);
    EXPECT_FLOAT_EQ(result->loader_settings_storage.value.scale, 0.5f);
}

TEST(AssetMetaDyn, SerializeBytes_ProcessedInfoPreserved) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action    = AssetActionType::Process;
    meta.processor = "P";
    AssetHash h{};
    h[0]           = 99;
    meta.processed = ProcessedInfo{.hash = h, .full_hash = h};

    auto bytes  = meta.serialize_bytes();
    auto result = deserialize_asset_meta<TestLoaderSettings, TestProcessSettings>(
        std::span<const std::byte>(bytes.data(), bytes.size()));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->processed.has_value());
    EXPECT_TRUE(result->processed->hash == h);
}

// ===========================================================================
// deserialize_processed_info — partial-read utility
// ===========================================================================

TEST(DeserializeProcessedInfo, NoPresentInfo_ReturnsNullopt) {
    // Serialize a meta with no ProcessedInfo then extract
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "SomeLoader";
    ASSERT_FALSE(meta.processed.has_value());

    auto bytes  = meta.serialize_bytes();
    auto result = deserialize_processed_info(std::span<const std::byte>(bytes.data(), bytes.size()));
    ASSERT_TRUE(result.has_value());    // function succeeded
    EXPECT_FALSE(result->has_value());  // no ProcessedInfo stored
}

TEST(DeserializeProcessedInfo, PresentInfo_RoundTrip) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "L";
    AssetHash h{};
    h[0]           = 5;
    h[31]          = 200;
    meta.processed = ProcessedInfo{.hash = h, .full_hash = h, .source_mtime_ns = 77};

    auto bytes  = meta.serialize_bytes();
    auto result = deserialize_processed_info(std::span<const std::byte>(bytes.data(), bytes.size()));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_TRUE((*result)->hash == h);
    EXPECT_TRUE((*result)->full_hash == h);
    ASSERT_TRUE((*result)->source_mtime_ns.has_value());
    EXPECT_EQ(*(*result)->source_mtime_ns, 77);
}

TEST(DeserializeProcessedInfo, WithDependencies_RoundTrip) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "L";
    AssetHash h{};
    h[0] = 11;
    ProcessDependencyInfo dep{.full_hash = h, .path = "dep.txt"};
    meta.processed = ProcessedInfo{.hash = h, .full_hash = h, .process_dependencies = {dep}};

    auto bytes  = meta.serialize_bytes();
    auto result = deserialize_processed_info(std::span<const std::byte>(bytes.data(), bytes.size()));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    ASSERT_EQ((*result)->process_dependencies.size(), 1u);
    EXPECT_EQ((*result)->process_dependencies[0].path, "dep.txt");
}

TEST(DeserializeProcessedInfo, VectorOverload_EquivalentToSpan) {
    AssetMeta<TestLoaderSettings, TestProcessSettings> meta;
    meta.action = AssetActionType::Load;
    meta.loader = "L";
    AssetHash h{};
    h[0]           = 3;
    meta.processed = ProcessedInfo{.hash = h};

    auto bytes    = meta.serialize_bytes();
    auto via_span = deserialize_processed_info(std::span<const std::byte>(bytes.data(), bytes.size()));
    auto via_vec  = deserialize_processed_info(bytes);
    ASSERT_TRUE(via_span.has_value());
    ASSERT_TRUE(via_vec.has_value());
    ASSERT_TRUE(via_span->has_value());
    ASSERT_TRUE(via_vec->has_value());
    EXPECT_TRUE((*via_span)->hash == (*via_vec)->hash);
}

TEST(DeserializeProcessedInfo, EmptyBytes_Fails) {
    std::vector<std::byte> empty;
    auto result = deserialize_processed_info(empty);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// AssetHasher / get_asset_hash / get_full_asset_hash — internal hashing helpers
// These are pub(crate) equivalents; tested indirectly through the processor
// pipeline in processor_embedded_tests. No direct unit tests here.
// ===========================================================================
