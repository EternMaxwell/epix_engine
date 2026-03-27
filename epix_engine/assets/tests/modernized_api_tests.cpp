#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// ErasedLoadedAsset — labels() range + take<A>()
// Note: Creating a populated ErasedLoadedAsset requires internal types
// (AssetContainerImpl) which are not exported. Tests are limited to
// default-constructed state and API shape verification.
// ===========================================================================

TEST(ErasedLoadedAsset_Labels, Empty) {
    ErasedLoadedAsset asset;
    auto lbl = asset.labels();
    EXPECT_TRUE(lbl.empty());
}

TEST(ErasedLoadedAsset_Take, NullValue) {
    ErasedLoadedAsset asset;
    auto taken = std::move(asset).take<int>();
    EXPECT_FALSE(taken.has_value());
}

// ===========================================================================
// LoadedAsset — labels() range
// ===========================================================================

TEST(LoadedAsset_Labels, Empty) {
    LoadedAsset<std::string> loaded(std::string("x"));
    int count = 0;
    for (auto&& label : loaded.labels()) {
        (void)label;
        count++;
    }
    EXPECT_EQ(count, 0);
}

// ===========================================================================
// TransformedAsset — labels() range
// ===========================================================================

TEST(TransformedAsset_Labels, Empty) {
    TransformedAsset<std::string> ta(std::string("x"));
    int count = 0;
    for (auto&& label : ta.labels()) {
        (void)label;
        count++;
    }
    EXPECT_EQ(count, 0);
}

// ===========================================================================
// SavedAsset — labels() range + from_transformed
// Note: SavedAsset direct constructor requires LabeledAsset (not exported).
// Testing via from_transformed which is constructible from test code.
// ===========================================================================

TEST(SavedAsset_FromTransformed, Basic) {
    TransformedAsset<std::string> ta(std::string("transformed_val"));
    auto saved = SavedAsset<std::string>::from_transformed(ta);
    EXPECT_EQ(saved.get(), "transformed_val");
}

TEST(SavedAsset_FromTransformed, LabelsEmpty) {
    TransformedAsset<std::string> ta(std::string("val"));
    auto saved = SavedAsset<std::string>::from_transformed(ta);
    int count  = 0;
    for (auto&& label : saved.labels()) {
        (void)label;
        count++;
    }
    EXPECT_EQ(count, 0);
}

TEST(SavedAsset_FromTransformed, Dereference) {
    TransformedAsset<std::string> ta(std::string("hello"));
    auto saved = SavedAsset<std::string>::from_transformed(ta);
    EXPECT_EQ(*saved, "hello");
    EXPECT_EQ(saved->size(), 5u);
}

// ===========================================================================
// ProcessContext — istream reader + ProcessedInfo
// ===========================================================================

TEST(ProcessContext, ReaderAccess) {
    auto sources = std::make_shared<AssetSources>();
    AssetServer server(sources);
    std::string data = "Hello, World!";
    auto reader      = std::make_unique<std::istringstream>(data);
    AssetPath path("test.txt");
    ProcessContext ctx(server, path, std::move(reader));

    std::string result;
    std::getline(ctx.asset_reader(), result);
    EXPECT_EQ(result, "Hello, World!");
}

TEST(ProcessContext, ProcessedInfoPresent) {
    auto sources = std::make_shared<AssetSources>();
    AssetServer server(sources);
    ProcessedInfo info;
    info.hash   = 12345;
    auto reader = std::make_unique<std::istringstream>("");
    AssetPath path("test.txt");
    ProcessContext ctx(server, path, std::move(reader), info);
    EXPECT_EQ(ctx.path(), path);
}

TEST(ProcessContext, PathAccess) {
    auto sources = std::make_shared<AssetSources>();
    AssetServer server(sources);
    auto reader = std::make_unique<std::istringstream>("");
    AssetPath path("assets/model.obj");
    ProcessContext ctx(server, path, std::move(reader));
    EXPECT_EQ(ctx.path(), path);
}

// ===========================================================================
// ProcessError — new variants
// ===========================================================================

TEST(ProcessError, MissingProcessor) {
    ProcessError err = process_error::MissingProcessor{.processor = "MyProcessor"};
    EXPECT_TRUE(std::holds_alternative<process_error::MissingProcessor>(err));
}

TEST(ProcessError, AmbiguousProcessor) {
    ProcessError err =
        process_error::AmbiguousProcessor{.processor_short_name = "Proc", .ambiguous_processor_names = {"A", "B"}};
    EXPECT_TRUE(std::holds_alternative<process_error::AmbiguousProcessor>(err));
}

TEST(ProcessError, WrongMetaType) {
    ProcessError err = process_error::WrongMetaType{};
    EXPECT_TRUE(std::holds_alternative<process_error::WrongMetaType>(err));
}

TEST(ProcessError, ExtensionRequired) {
    ProcessError err = process_error::ExtensionRequired{};
    EXPECT_TRUE(std::holds_alternative<process_error::ExtensionRequired>(err));
}

TEST(ProcessError, DeserializeMetaError) {
    ProcessError err = process_error::DeserializeMetaError{.message = "bad json"};
    EXPECT_TRUE(std::holds_alternative<process_error::DeserializeMetaError>(err));
    auto& dme = std::get<process_error::DeserializeMetaError>(err);
    EXPECT_EQ(dme.message, "bad json");
}

// ===========================================================================
// IdentityAssetTransformer (unchanged, but verify still works)
// ===========================================================================

TEST(IdentityAssetTransformer, Roundtrip) {
    IdentityAssetTransformer<std::string> t;
    typename IdentityAssetTransformer<std::string>::Settings s;
    auto result = t.transform(TransformedAsset<std::string>(std::string("data")), s);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get(), "data");
}
