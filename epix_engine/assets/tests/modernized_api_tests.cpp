#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.assets;

using namespace epix::assets;

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
// ProcessContext — Reader& + ProcessedInfo
// ===========================================================================

// Helper for creating a minimal AssetProcessor for ProcessContext tests
AssetProcessor make_test_processor() {
    AssetSourceBuilders builders;
    auto dir            = memory::Directory::create({});
    auto source_builder = AssetSourceBuilder::create([dir]() -> std::unique_ptr<AssetReader> {
                              return std::make_unique<MemoryAssetReader>(dir);
                          })
                              .with_processed_reader([dir]() -> std::unique_ptr<AssetReader> {
                                  return std::make_unique<MemoryAssetReader>(dir);
                              })
                              .with_processed_writer([dir]() -> std::unique_ptr<AssetWriter> {
                                  return std::make_unique<MemoryAssetWriter>(dir);
                              });
    builders.insert(AssetSourceId{}, std::move(source_builder));
    return ::epix::assets::AssetProcessor(builders, false);
}

TEST(ProcessContext, ReaderAccess) {
    auto processor = make_test_processor();
    VecReader data(std::string("Hello, World!"));
    AssetPath path("test.txt");
    ProcessedInfo info;
    ProcessContext ctx(processor, path, data, info);

    std::vector<uint8_t> buf;
    asio::io_context io;
    asio::co_spawn(
        io, [&]() -> asio::awaitable<void> { co_await ctx.asset_reader().read_to_end(buf); }, asio::detached);
    io.run();
    EXPECT_EQ(std::string(buf.begin(), buf.end()), "Hello, World!");
}

TEST(ProcessContext, ProcessedInfoPresent) {
    auto processor = make_test_processor();
    ProcessedInfo info;
    AssetHash h = {};
    h[0]        = 12345 & 0xFF;
    h[1]        = (12345 >> 8) & 0xFF;
    info.hash   = h;
    VecReader data(std::string(""));
    AssetPath path("test.txt");
    ProcessContext ctx(processor, path, data, info);
    EXPECT_EQ(ctx.path(), path);
}

TEST(ProcessContext, PathAccess) {
    auto processor = make_test_processor();
    VecReader data(std::string(""));
    AssetPath path("assets/model.obj");
    ProcessedInfo info;
    ProcessContext ctx(processor, path, data, info);
    EXPECT_EQ(ctx.path(), path);
}

// ===========================================================================
// ProcessError — new variants
// ===========================================================================

TEST(ProcessError, MissingProcessor) {
    ProcessError err{process_errors::MissingProcessor{"MyProcessor"}};
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingProcessor>(err));
}

TEST(ProcessError, AmbiguousProcessor) {
    ProcessError err{process_errors::AmbiguousProcessor{"Proc", {"A", "B"}}};
    EXPECT_TRUE(std::holds_alternative<process_errors::AmbiguousProcessor>(err));
}

TEST(ProcessError, WrongMetaType) {
    ProcessError err{process_errors::WrongMetaType{}};
    EXPECT_TRUE(std::holds_alternative<process_errors::WrongMetaType>(err));
}

TEST(ProcessError, ExtensionRequired) {
    ProcessError err{process_errors::ExtensionRequired{}};
    EXPECT_TRUE(std::holds_alternative<process_errors::ExtensionRequired>(err));
}

TEST(ProcessError, DeserializeMetaError) {
    ProcessError err{process_errors::DeserializeMetaError{"bad json"}};
    EXPECT_TRUE(std::holds_alternative<process_errors::DeserializeMetaError>(err));
    auto& dme = std::get<process_errors::DeserializeMetaError>(err);
    EXPECT_EQ(dme.msg, "bad json");
}

// ===========================================================================
// IdentityAssetTransformer (unchanged, but verify still works)
// ===========================================================================

TEST(IdentityAssetTransformer, Roundtrip) {
    IdentityAssetTransformer<std::string> t;
    typename IdentityAssetTransformer<std::string>::Settings s;
    asio::io_context io;
    std::expected<TransformedAsset<std::string>, std::exception_ptr> result = std::unexpected(std::exception_ptr{});
    asio::co_spawn(
        io,
        [&]() -> asio::awaitable<void> {
            result = co_await t.transform(TransformedAsset<std::string>(std::string("data")), s);
        },
        asio::detached);
    io.run();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get(), "data");
}
