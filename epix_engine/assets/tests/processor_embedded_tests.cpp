#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.assets;
import epix.core;
import epix.utils;
import epix.meta;

using namespace epix::assets;
namespace meta = epix::meta;

// ===========================================================================
// Test helper types - matching Bevy's test patterns
// ===========================================================================

namespace {

// ---- Helpers for reading memory VFS content ----

memory::Value make_val(std::string_view s) {
    auto sp  = std::as_bytes(std::span(s));
    auto buf = std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
    return memory::Value::from_shared(buf);
}

std::string read_dir_file(const memory::Directory& dir, const std::filesystem::path& path) {
    auto file = dir.get_file(path);
    if (!file.has_value()) return {};
    auto& v = file->value;
    if (std::holds_alternative<std::shared_ptr<std::vector<std::byte>>>(v.v)) {
        auto& buf = std::get<std::shared_ptr<std::vector<std::byte>>>(v.v);
        return std::string(reinterpret_cast<const char*>(buf->data()), buf->size());
    }
    if (std::holds_alternative<std::span<const std::byte>>(v.v)) {
        auto sp = std::get<std::span<const std::byte>>(v.v);
        return std::string(reinterpret_cast<const char*>(sp.data()), sp.size());
    }
    return {};
}

// ---- Test asset loader: loads string from stream ----

struct TestTextLoader {
    using Asset = std::string;
    struct Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"txt"}, std::string_view{"text"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static asio::awaitable<std::expected<std::string, Error>> load(Reader& reader, const Settings&, LoadContext&) {
        std::vector<uint8_t> buf;
        co_await reader.read_to_end(buf);
        co_return std::string(buf.begin(), buf.end());
    }
};

// ---- Test saver: writes string to stream ----

struct TestTextSaver {
    using Asset        = std::string;
    using OutputLoader = TestTextLoader;
    struct Settings {};
    using Error = std::exception_ptr;

    asio::awaitable<std::expected<OutputLoader::Settings, Error>> save(Writer& writer,
                                                                       SavedAsset<std::string> asset,
                                                                       const Settings&,
                                                                       const AssetPath&) const {
        auto& str = asset.get();
        std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(str.data()), str.size());
        co_await writer.write(data);
        co_return OutputLoader::Settings{};
    }
};

// ---- Test transformer: appends text to string asset ----

struct AddTextTransformer {
    using AssetInput  = std::string;
    using AssetOutput = std::string;
    struct Settings {};
    using Error = std::exception_ptr;

    std::string suffix;

    asio::awaitable<std::expected<TransformedAsset<std::string>, Error>> transform(TransformedAsset<std::string> asset,
                                                                                   const Settings&) const {
        asset.get_mut() += suffix;
        co_return asset;
    }
};

// ---- Test processor: LoadTransformAndSave<TestTextLoader, AddTextTransformer, TestTextSaver> ----

using TestLTSProcessor = LoadTransformAndSave<TestTextLoader, AddTextTransformer, TestTextSaver>;

// ---- Simple identity processor (no-op transform) ----

struct TestIdentityProcessor {
    struct Settings {};
    using OutputLoader = TestTextLoader;

    asio::awaitable<std::expected<OutputLoader::Settings, std::exception_ptr>> process(ProcessContext& ctx,
                                                                                       const Settings&,
                                                                                       Writer& writer) const {
        std::vector<uint8_t> buf;
        co_await ctx.asset_reader().read_to_end(buf);
        co_await writer.write(std::span<const uint8_t>(buf));
        co_return OutputLoader::Settings{};
    }
};

// ---- Helper: create an AssetProcessor with in-memory sources ----

AssetProcessor create_empty_asset_processor() {
    auto app = epix::core::App::create();
    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.build(app);

    auto processor = app.get_resource<AssetProcessor>();
    EXPECT_TRUE(processor.has_value());
    return processor->get();
}

}  // namespace

// ===========================================================================
// ProcessContext
// ===========================================================================

TEST(ProcessContext, Construction) {
    auto processor = create_empty_asset_processor();
    AssetPath path("test.txt");
    VecReader data(std::string("AB"));
    ProcessedInfo info;
    ProcessContext ctx(processor, path, data, info);
    EXPECT_EQ(ctx.path(), path);
    std::vector<uint8_t> buf;
    asio::io_context io;
    asio::co_spawn(
        io, [&]() -> asio::awaitable<void> { co_await ctx.asset_reader().read_to_end(buf); }, asio::detached);
    io.run();
    EXPECT_EQ(buf.size(), 2u);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[1], 'B');
}

TEST(ProcessContext, WithProcessedInfo) {
    auto processor = create_empty_asset_processor();
    AssetPath path("model.obj");
    VecReader data(std::string(""));
    ProcessedInfo info;
    ProcessContext ctx(processor, path, data, info);
    EXPECT_EQ(ctx.path(), path);
    AssetHash test_hash           = {};
    test_hash[0]                  = 42;
    ctx.new_processed_info().hash = test_hash;
    EXPECT_EQ(info.hash, test_hash);
}

// ===========================================================================
// ProcessError - variant-based
// ===========================================================================

TEST(ProcessError, MissingAssetLoaderForExtension) {
    ProcessError err{process_errors::MissingAssetLoaderForExtension{".xyz"}};
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingAssetLoaderForExtension>(err));
    EXPECT_EQ(std::get<process_errors::MissingAssetLoaderForExtension>(err).extension, ".xyz");
}

TEST(ProcessError, AssetReaderError) {
    ProcessError err{process_errors::AssetReaderError{
        AssetPath("test.txt"), epix::assets::AssetReaderError{reader_errors::NotFound{"test.txt"}}}};
    EXPECT_TRUE(std::holds_alternative<process_errors::AssetReaderError>(err));
}

TEST(ProcessError, MissingProcessor) {
    ProcessError err{process_errors::MissingProcessor{"not_found"}};
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingProcessor>(err));
    EXPECT_EQ(std::get<process_errors::MissingProcessor>(err).name, "not_found");
}

TEST(ProcessError, ExtensionRequired) {
    ProcessError err{process_errors::ExtensionRequired{}};
    EXPECT_TRUE(std::holds_alternative<process_errors::ExtensionRequired>(err));
}

// ===========================================================================
// ProcessResult
// ===========================================================================

TEST(ProcessResult, MakeProcessed) {
    ProcessedInfo info;
    AssetHash test_hash = {};
    test_hash[0]        = 123;
    info.hash           = test_hash;
    auto result         = ProcessResult::make_processed(std::move(info));
    EXPECT_EQ(result.kind, ProcessResultKind::Processed);
    ASSERT_TRUE(result.processed_info.has_value());
    EXPECT_EQ(result.processed_info->hash, test_hash);
}

TEST(ProcessResult, SkippedNotChanged) {
    auto result = ProcessResult::skipped_not_changed();
    EXPECT_EQ(result.kind, ProcessResultKind::SkippedNotChanged);
    EXPECT_FALSE(result.processed_info.has_value());
}

TEST(ProcessResult, Ignored) {
    auto result = ProcessResult::ignored();
    EXPECT_EQ(result.kind, ProcessResultKind::Ignored);
}

// ===========================================================================
// AssetProcessor - register and lookup (matches Bevy's get_asset_processor_by_name)
// ===========================================================================

TEST(AssetProcessor, RegisterAndGetProcessor) {
    auto processor = create_empty_asset_processor();
    processor.register_processor(TestIdentityProcessor{});
    // Should be findable by full type path
    auto by_full = processor.get_processor(meta::type_id<TestIdentityProcessor>{}.name());
    ASSERT_TRUE(by_full.has_value());
    EXPECT_NE(*by_full, nullptr);
}

TEST(AssetProcessor, GetProcessor_Missing) {
    auto processor = create_empty_asset_processor();
    auto result    = processor.get_processor("NonExistent");
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<get_processor_errors::Missing>(result.error()));
}

TEST(AssetProcessor, SetAndGetDefaultProcessor) {
    auto processor = create_empty_asset_processor();
    processor.register_processor(TestIdentityProcessor{});
    processor.set_default_processor<TestIdentityProcessor>("txt");
    auto result = processor.get_default_processor("txt");
    EXPECT_NE(result, nullptr);
}

TEST(AssetProcessor, GetDefaultProcessor_MissingExtension) {
    auto processor = create_empty_asset_processor();
    auto result    = processor.get_default_processor("unknown");
    EXPECT_EQ(result, nullptr);
}

// ---- Bevy's get_asset_processor_by_name: short + long path lookup ----
// Matches bevy_asset::processor::tests::get_asset_processor_by_name

TEST(AssetProcessor, GetProcessorByName_ShortAndLongPath) {
    auto processor = create_empty_asset_processor();
    processor.register_processor(TestIdentityProcessor{});

    auto full_name  = std::string(meta::type_id<TestIdentityProcessor>{}.name());
    auto short_name = std::string(meta::type_id<TestIdentityProcessor>{}.short_name());

    auto by_long = processor.get_processor(full_name);
    ASSERT_TRUE(by_long.has_value()) << "Processor should be findable by full type path";
    auto by_short = processor.get_processor(short_name);
    ASSERT_TRUE(by_short.has_value()) << "Processor should be findable by short type path";

    // Both should return the same processor instance
    EXPECT_EQ(by_long->get(), by_short->get());
}

// ---- Bevy's missing_processor_returns_error ----

TEST(AssetProcessor, MissingProcessor_LongPath_ReturnsError) {
    auto processor = create_empty_asset_processor();
    auto full_name = std::string(meta::type_id<TestIdentityProcessor>{}.name());

    auto result = processor.get_processor(full_name);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<get_processor_errors::Missing>(result.error()));
    EXPECT_EQ(std::get<get_processor_errors::Missing>(result.error()).name, full_name);
}

TEST(AssetProcessor, MissingProcessor_ShortPath_ReturnsError) {
    auto processor  = create_empty_asset_processor();
    auto short_name = std::string(meta::type_id<TestIdentityProcessor>{}.short_name());

    auto result = processor.get_processor(short_name);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<get_processor_errors::Missing>(result.error()));
    EXPECT_EQ(std::get<get_processor_errors::Missing>(result.error()).name, short_name);
}

// ---- Bevy's ambiguous_short_path_returns_error ----
// Two processors with different full names but the same short name.

namespace ambiguity_ns1 {
struct AmbigMarker {};
}  // namespace ambiguity_ns1
namespace ambiguity_ns2 {
struct AmbigMarker {};
}  // namespace ambiguity_ns2

template <typename T>
struct TemplatedProcessor {
    struct Settings {};
    using OutputLoader = TestTextLoader;

    asio::awaitable<std::expected<OutputLoader::Settings, std::exception_ptr>> process(ProcessContext&,
                                                                                       const Settings&,
                                                                                       Writer& writer) const {
        std::string_view hello = "hello";
        co_await writer.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(hello.data()), hello.size()));
        co_return OutputLoader::Settings{};
    }
};

TEST(AssetProcessor, AmbiguousShortPath_ReturnsError) {
    auto processor = create_empty_asset_processor();
    processor.register_processor(TemplatedProcessor<ambiguity_ns1::AmbigMarker>{});
    processor.register_processor(TemplatedProcessor<ambiguity_ns2::AmbigMarker>{});

    auto short_name_1 = meta::type_id<TemplatedProcessor<ambiguity_ns1::AmbigMarker>>{}.short_name();
    auto short_name_2 = meta::type_id<TemplatedProcessor<ambiguity_ns2::AmbigMarker>>{}.short_name();
    // Both should have the same short name (namespaces stripped)
    ASSERT_EQ(short_name_1, short_name_2) << "Short names should collide for the ambiguity test to work";

    // Lookup by short name should fail with Ambiguous
    auto result = processor.get_processor(short_name_1);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<get_processor_errors::Ambiguous>(result.error()));

    auto& ambig = std::get<get_processor_errors::Ambiguous>(result.error());
    EXPECT_EQ(ambig.processor_short_name, std::string(short_name_1));
    EXPECT_EQ(ambig.ambiguous_processor_names.size(), 2u);

    // But lookup by full name should succeed and return different processors
    auto full_name_1 = meta::type_id<TemplatedProcessor<ambiguity_ns1::AmbigMarker>>{}.name();
    auto full_name_2 = meta::type_id<TemplatedProcessor<ambiguity_ns2::AmbigMarker>>{}.name();
    ASSERT_NE(full_name_1, full_name_2) << "Full names should differ";

    auto proc1 = processor.get_processor(full_name_1);
    auto proc2 = processor.get_processor(full_name_2);
    ASSERT_TRUE(proc1.has_value()) << "Processor should be findable by full type path";
    ASSERT_TRUE(proc2.has_value()) << "Processor should be findable by full type path";
    EXPECT_NE(proc1->get(), proc2->get()) << "Different full names should yield different processors";
}

// ===========================================================================
// EmbeddedAssetRegistry
// ===========================================================================

TEST(EmbeddedAssetRegistry, InsertAndRetrieve) {
    EmbeddedAssetRegistry registry;
    auto data = std::as_bytes(std::span("Hello", 5));
    registry.insert_asset("test.txt", data);
    auto& dir = registry.directory();
    auto file = dir.get_file("test.txt");
    ASSERT_TRUE(file.has_value());
}

TEST(EmbeddedAssetRegistry, InsertStatic) {
    EmbeddedAssetRegistry registry;
    static const std::byte data[] = {std::byte{0x41}, std::byte{0x42}, std::byte{0x43}};
    registry.insert_asset_static("abc.bin", std::span(data));
    auto& dir = registry.directory();
    EXPECT_TRUE(dir.exists("abc.bin").value_or(false));
}

TEST(EmbeddedAssetRegistry, InsertMeta) {
    EmbeddedAssetRegistry registry;
    auto meta = std::as_bytes(std::span("{}", 2));
    registry.insert_meta("test.txt", meta);
    auto& dir = registry.directory();
    EXPECT_TRUE(dir.exists("test.txt.meta").value_or(false));
}

TEST(EmbeddedAssetRegistry, RemoveAsset_Existing) {
    EmbeddedAssetRegistry registry;
    auto data = std::as_bytes(std::span("\x01", 1));
    registry.insert_asset("test.bin", data);
    EXPECT_TRUE(registry.remove_asset("test.bin"));
    EXPECT_FALSE(registry.directory().exists("test.bin").value_or(true));
}

TEST(EmbeddedAssetRegistry, RemoveAsset_Missing) {
    EmbeddedAssetRegistry registry;
    EXPECT_FALSE(registry.remove_asset("nonexistent.bin"));
}

TEST(EmbeddedAssetRegistry, DirectoryAccess_Mutable) {
    EmbeddedAssetRegistry registry;
    auto& dir = registry.directory();
    // Mutable access should also work
    (void)dir.create_directory("subdir");
    EXPECT_TRUE(dir.is_directory("subdir").value_or(false));
}

// ===========================================================================
// ProcessError - remaining variants
// ===========================================================================

TEST(ProcessError, AmbiguousProcessor) {
    ProcessError err{process_errors::AmbiguousProcessor{"Foo", {"ns1::Foo", "ns2::Foo"}}};
    ASSERT_TRUE(std::holds_alternative<process_errors::AmbiguousProcessor>(err));
    auto& v = std::get<process_errors::AmbiguousProcessor>(err);
    EXPECT_EQ(v.processor_short_name, "Foo");
    EXPECT_EQ(v.ambiguous_processor_names.size(), 2u);
}

TEST(ProcessError, AssetWriterError) {
    ProcessError err{process_errors::AssetWriterError{
        AssetPath("out.bin"),
        epix::assets::AssetWriterError{writer_errors::IoError{std::make_error_code(std::errc::no_space_on_device)}}}};
    EXPECT_TRUE(std::holds_alternative<process_errors::AssetWriterError>(err));
}

TEST(ProcessError, MissingProcessedAssetReader) {
    ProcessError err{process_errors::MissingProcessedAssetReader{}};
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingProcessedAssetReader>(err));
}

TEST(ProcessError, MissingProcessedAssetWriter) {
    ProcessError err{process_errors::MissingProcessedAssetWriter{}};
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingProcessedAssetWriter>(err));
}

TEST(ProcessError, ReadAssetMetaError) {
    ProcessError err{process_errors::ReadAssetMetaError{
        AssetPath("test.txt"), epix::assets::AssetReaderError{reader_errors::NotFound{"meta"}}}};
    EXPECT_TRUE(std::holds_alternative<process_errors::ReadAssetMetaError>(err));
}

TEST(ProcessError, DeserializeMetaError) {
    ProcessError err{process_errors::DeserializeMetaError{"bad json"}};
    ASSERT_TRUE(std::holds_alternative<process_errors::DeserializeMetaError>(err));
    EXPECT_EQ(std::get<process_errors::DeserializeMetaError>(err).msg, "bad json");
}

TEST(ProcessError, AssetLoadError) {
    try {
        throw std::runtime_error("load failed");
    } catch (...) {
        ProcessError err{process_errors::AssetLoadError{std::current_exception()}};
        EXPECT_TRUE(std::holds_alternative<process_errors::AssetLoadError>(err));
    }
}

TEST(ProcessError, WrongMetaType) {
    ProcessError err{process_errors::WrongMetaType{}};
    EXPECT_TRUE(std::holds_alternative<process_errors::WrongMetaType>(err));
}

TEST(ProcessError, AssetSaveError) {
    try {
        throw std::runtime_error("save failed");
    } catch (...) {
        ProcessError err{process_errors::AssetSaveError{std::current_exception()}};
        EXPECT_TRUE(std::holds_alternative<process_errors::AssetSaveError>(err));
    }
}

TEST(ProcessError, AssetTransformError) {
    try {
        throw std::runtime_error("transform failed");
    } catch (...) {
        ProcessError err{process_errors::AssetTransformError{std::current_exception()}};
        EXPECT_TRUE(std::holds_alternative<process_errors::AssetTransformError>(err));
    }
}

// ===========================================================================
// ProcessError - visitor dispatch (matches Bevy's pattern-matching style)
// ===========================================================================

TEST(ProcessError, VisitorDispatch) {
    ProcessError err{process_errors::MissingProcessor{"proc_x"}};
    bool matched = std::visit(epix::utils::visitor{
                                  [](const process_errors::MissingProcessor& e) { return e.name == "proc_x"; },
                                  [](const auto&) { return false; },
                              },
                              err);
    EXPECT_TRUE(matched);
}

// ===========================================================================
// GetProcessorError - variant coverage
// ===========================================================================

TEST(GetProcessorError, Missing) {
    GetProcessorError err{get_processor_errors::Missing{"some_processor"}};
    ASSERT_TRUE(std::holds_alternative<get_processor_errors::Missing>(err));
    EXPECT_EQ(std::get<get_processor_errors::Missing>(err).name, "some_processor");
}

TEST(GetProcessorError, Ambiguous) {
    GetProcessorError err{get_processor_errors::Ambiguous{"Foo", {"ns1::Foo", "ns2::Foo"}}};
    ASSERT_TRUE(std::holds_alternative<get_processor_errors::Ambiguous>(err));
    auto& v = std::get<get_processor_errors::Ambiguous>(err);
    EXPECT_EQ(v.processor_short_name, "Foo");
    EXPECT_EQ(v.ambiguous_processor_names.size(), 2u);
}

TEST(GetProcessorError, VisitorDispatch) {
    GetProcessorError err{get_processor_errors::Missing{"x"}};
    bool matched = std::visit(epix::utils::visitor{
                                  [](const get_processor_errors::Missing&) { return true; },
                                  [](const get_processor_errors::Ambiguous&) { return false; },
                              },
                              err);
    EXPECT_TRUE(matched);
}

// ===========================================================================
// LogEntryError - variant coverage
// ===========================================================================

TEST(LogEntryError, DuplicateTransaction) {
    LogEntryError err{log_entry_errors::DuplicateTransaction{AssetPath("dup.txt")}};
    ASSERT_TRUE(std::holds_alternative<log_entry_errors::DuplicateTransaction>(err));
    EXPECT_EQ(std::get<log_entry_errors::DuplicateTransaction>(err).path, AssetPath("dup.txt"));
}

TEST(LogEntryError, EndedMissingTransaction) {
    LogEntryError err{log_entry_errors::EndedMissingTransaction{AssetPath("missing.txt")}};
    ASSERT_TRUE(std::holds_alternative<log_entry_errors::EndedMissingTransaction>(err));
    EXPECT_EQ(std::get<log_entry_errors::EndedMissingTransaction>(err).path, AssetPath("missing.txt"));
}

TEST(LogEntryError, UnfinishedTransaction) {
    LogEntryError err{log_entry_errors::UnfinishedTransaction{AssetPath("unfinished.txt")}};
    ASSERT_TRUE(std::holds_alternative<log_entry_errors::UnfinishedTransaction>(err));
    EXPECT_EQ(std::get<log_entry_errors::UnfinishedTransaction>(err).path, AssetPath("unfinished.txt"));
}

// ===========================================================================
// ValidateLogError - variant coverage
// ===========================================================================

TEST(ValidateLogError, UnrecoverableError) {
    ValidateLogError err{validate_log_errors::UnrecoverableError{}};
    EXPECT_TRUE(std::holds_alternative<validate_log_errors::UnrecoverableError>(err));
}

TEST(ValidateLogError, ReadLogError) {
    ValidateLogError err{validate_log_errors::ReadLogError{"io failure"}};
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::ReadLogError>(err));
    EXPECT_EQ(std::get<validate_log_errors::ReadLogError>(err).msg, "io failure");
}

TEST(ValidateLogError, EntryErrors) {
    ValidateLogError err{
        validate_log_errors::EntryErrors{{LogEntryError{log_entry_errors::DuplicateTransaction{AssetPath("x.txt")}}}}};
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::EntryErrors>(err));
    EXPECT_EQ(std::get<validate_log_errors::EntryErrors>(err).errors.size(), 1u);
}
