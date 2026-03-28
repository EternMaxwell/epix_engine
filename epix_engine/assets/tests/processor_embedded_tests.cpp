#include <gtest/gtest.h>

import std;
import epix.utils;
import epix.assets;

using namespace assets;

// ===========================================================================
// Test helper types — matching Bevy's test patterns
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

// ---- FakeTransactionLog — matches Bevy's FakeTransactionLog ----

struct FakeTransactionLog : ProcessorTransactionLog {
    std::expected<void, std::string> begin_processing(const AssetPath&) override { return {}; }
    std::expected<void, std::string> end_processing(const AssetPath&) override { return {}; }
    std::expected<void, std::string> unrecoverable() override { return {}; }
};

struct FakeTransactionLogFactory : ProcessorTransactionLogFactory {
    std::expected<std::vector<LogEntry>, std::string> read() const override { return std::vector<LogEntry>{}; }
    std::expected<std::unique_ptr<ProcessorTransactionLog>, std::string> create_new_log() const override {
        return std::make_unique<FakeTransactionLog>();
    }
};

// ---- Test asset loader: loads string from stream ----

struct TestTextLoader {
    using Asset = std::string;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"txt"}, std::string_view{"text"}};
        return exts;
    }

    static std::expected<std::string, Error> load(std::istream& reader, const Settings&, LoadContext&) {
        std::stringstream ss;
        ss << reader.rdbuf();
        return ss.str();
    }
};

// ---- Test saver: writes string to stream ----

struct TestTextSaver {
    using AssetType    = std::string;
    using OutputLoader = TestTextLoader;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    std::expected<OutputLoader::Settings, Error> save(std::ostream& writer,
                                                      SavedAsset<std::string> asset,
                                                      const Settings&,
                                                      const AssetPath&) const {
        writer << asset.get();
        return OutputLoader::Settings{};
    }
};

// ---- Test transformer: appends text to string asset ----

struct AddTextTransformer {
    using AssetInput  = std::string;
    using AssetOutput = std::string;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    std::string suffix;

    std::expected<TransformedAsset<std::string>, Error> transform(TransformedAsset<std::string> asset,
                                                                  const Settings&) const {
        asset.get_mut() += suffix;
        return asset;
    }
};

// ---- Test processor: LoadTransformAndSave<TestTextLoader, AddTextTransformer, TestTextSaver> ----

using TestLTSProcessor = LoadTransformAndSave<TestTextLoader, AddTextTransformer, TestTextSaver>;

// ---- Simple identity processor (no-op transform) ----

struct TestIdentityProcessor {
    using Settings     = assets::Settings;
    using OutputLoader = TestTextLoader;

    std::expected<OutputLoader::Settings, std::exception_ptr> process(ProcessContext& ctx,
                                                                      const Settings&,
                                                                      std::ostream& writer) const {
        writer << ctx.asset_reader().rdbuf();
        return OutputLoader::Settings{};
    }
};

// ---- Helper: create an AssetProcessor with in-memory sources ----

AssetProcessor create_empty_asset_processor() {
    auto data           = std::make_shared<AssetProcessorData>();
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
    data->source_builders->insert(AssetSourceId{}, std::move(source_builder));
    data->set_log_factory(std::make_shared<FakeTransactionLogFactory>());
    return AssetProcessor(data, false);
}

struct ProcessorTestEnv {
    memory::Directory source_dir;
    memory::Directory processed_dir;
    AssetProcessor processor;
};

ProcessorTestEnv create_processor_with_dirs() {
    auto source_dir    = memory::Directory::create({});
    auto processed_dir = memory::Directory::create({});
    auto data          = std::make_shared<AssetProcessorData>();
    auto sd            = source_dir;
    auto pd            = processed_dir;
    auto source_builder =
        AssetSourceBuilder::create(
            [sd]() -> std::unique_ptr<AssetReader> { return std::make_unique<MemoryAssetReader>(sd); })
            .with_writer([sd]() -> std::unique_ptr<AssetWriter> { return std::make_unique<MemoryAssetWriter>(sd); })
            .with_processed_reader(
                [pd]() -> std::unique_ptr<AssetReader> { return std::make_unique<MemoryAssetReader>(pd); })
            .with_processed_writer(
                [pd]() -> std::unique_ptr<AssetWriter> { return std::make_unique<MemoryAssetWriter>(pd); });
    data->source_builders->insert(AssetSourceId{}, std::move(source_builder));
    data->set_log_factory(std::make_shared<FakeTransactionLogFactory>());
    auto processor = AssetProcessor(data, false);
    return {source_dir, processed_dir, processor};
}

}  // namespace

// ===========================================================================
// ProcessContext
// ===========================================================================

TEST(ProcessContext, Construction) {
    auto processor = create_empty_asset_processor();
    AssetPath path("test.txt");
    std::istringstream data("AB");
    ProcessedInfo info;
    ProcessContext ctx(processor, path, data, info);
    EXPECT_EQ(ctx.path(), path);
    char buf[2];
    ctx.asset_reader().read(buf, 2);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[1], 'B');
}

TEST(ProcessContext, WithProcessedInfo) {
    auto processor = create_empty_asset_processor();
    AssetPath path("model.obj");
    std::istringstream data("");
    ProcessedInfo info;
    ProcessContext ctx(processor, path, data, info);
    EXPECT_EQ(ctx.path(), path);
    // new_processed_info should be accessible
    ctx.new_processed_info().hash = 42;
    EXPECT_EQ(info.hash, 42);
}

// ===========================================================================
// ProcessError — variant-based
// ===========================================================================

TEST(ProcessError, MissingAssetLoaderForExtension) {
    ProcessError err{process_errors::MissingAssetLoaderForExtension{".xyz"}};
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingAssetLoaderForExtension>(err));
    EXPECT_EQ(std::get<process_errors::MissingAssetLoaderForExtension>(err).extension, ".xyz");
}

TEST(ProcessError, AssetReaderError) {
    ProcessError err{process_errors::AssetReaderError{AssetPath("test.txt"),
                                                      assets::AssetReaderError{reader_errors::NotFound{"test.txt"}}}};
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
    info.hash   = 123;
    auto result = ProcessResult::make_processed(std::move(info));
    EXPECT_EQ(result.kind, ProcessResultKind::Processed);
    ASSERT_TRUE(result.processed_info.has_value());
    EXPECT_EQ(result.processed_info->hash, 123);
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
// AssetProcessor — register and lookup (matches Bevy's get_asset_processor_by_name)
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
    using Settings     = assets::Settings;
    using OutputLoader = TestTextLoader;

    std::expected<OutputLoader::Settings, std::exception_ptr> process(ProcessContext&,
                                                                      const Settings&,
                                                                      std::ostream& writer) const {
        writer << "hello";
        return OutputLoader::Settings{};
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
    registry.insert_asset("C:/assets/test.txt", "test.txt", data);
    auto& dir = registry.directory();
    auto file = dir.get_file("test.txt");
    ASSERT_TRUE(file.has_value());
}

TEST(EmbeddedAssetRegistry, InsertStatic) {
    EmbeddedAssetRegistry registry;
    static const std::byte data[] = {std::byte{0x41}, std::byte{0x42}, std::byte{0x43}};
    registry.insert_asset_static("C:/assets/abc.bin", "abc.bin", std::span(data));
    auto& dir = registry.directory();
    EXPECT_TRUE(dir.exists("abc.bin").value_or(false));
}

TEST(EmbeddedAssetRegistry, InsertMeta) {
    EmbeddedAssetRegistry registry;
    auto meta = std::as_bytes(std::span("{}", 2));
    registry.insert_meta("C:/assets/test.txt", "test.txt", meta);
    auto& dir = registry.directory();
    EXPECT_TRUE(dir.exists("test.txt.meta").value_or(false));
}

TEST(EmbeddedAssetRegistry, RemoveAsset_Existing) {
    EmbeddedAssetRegistry registry;
    auto data = std::as_bytes(std::span("\x01", 1));
    registry.insert_asset("C:/test.bin", "test.bin", data);
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
    dir.create_directory("subdir");
    EXPECT_TRUE(dir.is_directory("subdir").value_or(false));
}

// ===========================================================================
// ProcessError — remaining variants
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
        assets::AssetWriterError{writer_errors::IoError{std::make_error_code(std::errc::no_space_on_device)}}}};
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
    ProcessError err{process_errors::ReadAssetMetaError{AssetPath("test.txt"),
                                                        assets::AssetReaderError{reader_errors::NotFound{"meta"}}}};
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
// ProcessError — visitor dispatch (matches Bevy's pattern-matching style)
// ===========================================================================

TEST(ProcessError, VisitorDispatch) {
    ProcessError err{process_errors::MissingProcessor{"proc_x"}};
    bool matched = std::visit(utils::visitor{
                                  [](const process_errors::MissingProcessor& e) { return e.name == "proc_x"; },
                                  [](const auto&) { return false; },
                              },
                              err);
    EXPECT_TRUE(matched);
}

// ===========================================================================
// GetProcessorError — variant coverage
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
    bool matched = std::visit(utils::visitor{
                                  [](const get_processor_errors::Missing&) { return true; },
                                  [](const get_processor_errors::Ambiguous&) { return false; },
                              },
                              err);
    EXPECT_TRUE(matched);
}

// ===========================================================================
// validate_transaction_log — edge cases (Bevy tests these implicitly)
// ===========================================================================

namespace {

struct ConfigurableLogFactory : ProcessorTransactionLogFactory {
    std::vector<LogEntry> entries;
    bool read_fails = false;

    std::expected<std::vector<LogEntry>, std::string> read() const override {
        if (read_fails) return std::unexpected(std::string("disk error"));
        return entries;
    }
    std::expected<std::unique_ptr<ProcessorTransactionLog>, std::string> create_new_log() const override {
        return std::make_unique<FakeTransactionLog>();
    }
};

}  // namespace

TEST(ValidateTransactionLog, EmptyLog_Succeeds) {
    ConfigurableLogFactory factory;
    auto result = validate_transaction_log(factory);
    EXPECT_TRUE(result.has_value());
}

TEST(ValidateTransactionLog, MatchedBeginEnd_Succeeds) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::end_processing(AssetPath("a.txt")));
    auto result = validate_transaction_log(factory);
    EXPECT_TRUE(result.has_value());
}

TEST(ValidateTransactionLog, MultipleMatchedPairs_Succeeds) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("b.txt")));
    factory.entries.push_back(LogEntry::end_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::end_processing(AssetPath("b.txt")));
    auto result = validate_transaction_log(factory);
    EXPECT_TRUE(result.has_value());
}

TEST(ValidateTransactionLog, ReadFailure_ReturnsReadLogError) {
    ConfigurableLogFactory factory;
    factory.read_fails = true;
    auto result        = validate_transaction_log(factory);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::ReadLogError>(result.error()));
    EXPECT_EQ(std::get<validate_log_errors::ReadLogError>(result.error()).msg, "disk error");
}

TEST(ValidateTransactionLog, UnrecoverableEntry) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::unrecoverable_error());
    auto result = validate_transaction_log(factory);
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<validate_log_errors::UnrecoverableError>(result.error()));
}

TEST(ValidateTransactionLog, DuplicateTransaction) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    auto result = validate_transaction_log(factory);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::EntryErrors>(result.error()));
    auto& errs = std::get<validate_log_errors::EntryErrors>(result.error()).errors;
    ASSERT_GE(errs.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<log_entry_errors::DuplicateTransaction>(errs[0]));
}

TEST(ValidateTransactionLog, EndedMissingTransaction) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::end_processing(AssetPath("a.txt")));
    auto result = validate_transaction_log(factory);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::EntryErrors>(result.error()));
    auto& errs = std::get<validate_log_errors::EntryErrors>(result.error()).errors;
    ASSERT_GE(errs.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<log_entry_errors::EndedMissingTransaction>(errs[0]));
}

TEST(ValidateTransactionLog, UnfinishedTransaction) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    // No end entry → unfinished
    auto result = validate_transaction_log(factory);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::EntryErrors>(result.error()));
    auto& errs = std::get<validate_log_errors::EntryErrors>(result.error()).errors;
    ASSERT_GE(errs.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<log_entry_errors::UnfinishedTransaction>(errs[0]));
}

TEST(ValidateTransactionLog, MixedErrors_DuplicateAndUnfinished) {
    ConfigurableLogFactory factory;
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("a.txt")));
    factory.entries.push_back(LogEntry::begin_processing(AssetPath("b.txt")));
    // a.txt: duplicate. b.txt: unfinished. a.txt also unfinished.
    auto result = validate_transaction_log(factory);
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<validate_log_errors::EntryErrors>(result.error()));
    auto& errs = std::get<validate_log_errors::EntryErrors>(result.error()).errors;
    EXPECT_GE(errs.size(), 2u);
}

// ===========================================================================
// LogEntryError — variant coverage
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
// ValidateLogError — variant coverage
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

// ===========================================================================
// process_asset_internal — pipeline tests (matches Bevy's processing tests)
// ===========================================================================

TEST(ProcessAssetInternal, ExtensionRequired_NoExtension) {
    auto env        = create_processor_with_dirs();
    auto source_opt = env.processor.get_source(AssetSourceId{});
    ASSERT_TRUE(source_opt.has_value());
    auto& source = source_opt->get();

    // Write a file with no extension
    env.source_dir.insert_file("noext", make_val("data"));

    AssetPath path("noext");
    auto result = env.processor.process_asset_internal(source, path);
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<process_errors::ExtensionRequired>(result.error()));
}

TEST(ProcessAssetInternal, MissingLoader_NoProcessorOrLoader) {
    auto env        = create_processor_with_dirs();
    auto source_opt = env.processor.get_source(AssetSourceId{});
    ASSERT_TRUE(source_opt.has_value());
    auto& source = source_opt->get();

    // Write a file with an extension that has no registered loader or processor
    env.source_dir.insert_file("test.xyz", make_val("data"));

    AssetPath path("test.xyz");
    auto result = env.processor.process_asset_internal(source, path);
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<process_errors::MissingAssetLoaderForExtension>(result.error()));
}

TEST(ProcessAssetInternal, CopyThrough_WithLoader) {
    auto env = create_processor_with_dirs();
    env.processor.get_server().register_loader(TestTextLoader{});
    auto source_opt = env.processor.get_source(AssetSourceId{});
    ASSERT_TRUE(source_opt.has_value());
    auto& source = source_opt->get();

    // Write a source file
    env.source_dir.insert_file("hello.txt", make_val("hello world"));

    AssetPath path("hello.txt");
    auto result = env.processor.process_asset_internal(source, path);
    ASSERT_TRUE(result.has_value()) << "Copy-through should succeed";
    EXPECT_EQ(result->kind, ProcessResultKind::Processed);

    // Verify the file was copied to the processed directory
    auto processed_content = read_dir_file(env.processed_dir, "hello.txt");
    EXPECT_EQ(processed_content, "hello world");
}

TEST(ProcessAssetInternal, DefaultProcessor_TransformsAsset) {
    auto env = create_processor_with_dirs();
    env.processor.get_server().register_loader(TestTextLoader{});
    env.processor.register_processor(TestLTSProcessor(AddTextTransformer{" [processed]"}, TestTextSaver{}));
    env.processor.set_default_processor<TestLTSProcessor>("txt");
    auto source_opt = env.processor.get_source(AssetSourceId{});
    ASSERT_TRUE(source_opt.has_value());
    auto& source = source_opt->get();

    // Write a source file
    env.source_dir.insert_file("greet.txt", make_val("hello"));

    AssetPath path("greet.txt");
    auto result = env.processor.process_asset_internal(source, path);
    ASSERT_TRUE(result.has_value()) << "Processing should succeed";
    EXPECT_EQ(result->kind, ProcessResultKind::Processed);

    // Verify the processed output has the transformer's suffix
    auto processed_content = read_dir_file(env.processed_dir, "greet.txt");
    EXPECT_EQ(processed_content, "hello [processed]");
}

TEST(ProcessAssetInternal, AssetReadError_MissingSourceFile) {
    auto env        = create_processor_with_dirs();
    auto source_opt = env.processor.get_source(AssetSourceId{});
    ASSERT_TRUE(source_opt.has_value());
    auto& source = source_opt->get();

    env.processor.get_server().register_loader(TestTextLoader{});
    env.processor.register_processor(TestLTSProcessor(AddTextTransformer{""}, TestTextSaver{}));
    env.processor.set_default_processor<TestLTSProcessor>("txt");

    // Don't create the source file — should fail with AssetReaderError
    AssetPath path("missing.txt");
    auto result = env.processor.process_asset_internal(source, path);
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<process_errors::AssetReaderError>(result.error()));
}
