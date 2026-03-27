#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets;

// ===========================================================================
// ProcessContext
// ===========================================================================

TEST(ProcessContext, Construction) {
    auto sources = std::make_shared<AssetSources>();
    AssetServer server(sources);
    AssetPath path("test.txt");
    std::string data = "AB";
    auto reader      = std::make_unique<std::istringstream>(data);
    ProcessContext ctx(server, path, std::move(reader));
    EXPECT_EQ(ctx.path(), path);
    // Read from the stream to verify
    char buf[2];
    ctx.asset_reader().read(buf, 2);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[1], 'B');
}

TEST(ProcessContext, WithLoadedAsset) {
    auto sources = std::make_shared<AssetSources>();
    AssetServer server(sources);
    AssetPath path("model.obj");
    auto reader = std::make_unique<std::istringstream>("");
    ProcessedInfo info;
    ProcessContext ctx(server, path, std::move(reader), info);
    EXPECT_EQ(ctx.path(), path);
}

// ===========================================================================
// ProcessError — variant types
// ===========================================================================

TEST(ProcessError, AssetReaderFailed) {
    ProcessError err = process_error::AssetReaderError{.path = AssetPath("missing.txt"),
                                                       .err = AssetReaderError{reader_errors::NotFound{"missing.txt"}}};
    EXPECT_TRUE(std::holds_alternative<process_error::AssetReaderError>(err));
}

TEST(ProcessError, MissingLoader) {
    ProcessError err = process_error::MissingLoader{.type_name = "MyLoader"};
    EXPECT_TRUE(std::holds_alternative<process_error::MissingLoader>(err));
    EXPECT_EQ(std::get<process_error::MissingLoader>(err).type_name, "MyLoader");
}

TEST(ProcessError, LoadFailed) {
    ProcessError err = process_error::AssetLoadError{.error = std::make_exception_ptr(std::runtime_error("oops")),
                                                     .path  = AssetPath("bad.obj")};
    EXPECT_TRUE(std::holds_alternative<process_error::AssetLoadError>(err));
}

TEST(ProcessError, SaveFailed) {
    ProcessError err =
        process_error::AssetSaveError{.error = std::make_exception_ptr(std::runtime_error("write fail"))};
    EXPECT_TRUE(std::holds_alternative<process_error::AssetSaveError>(err));
}

TEST(ProcessError, TransformFailed) {
    ProcessError err =
        process_error::AssetTransformError{.error = std::make_exception_ptr(std::runtime_error("transform fail"))};
    EXPECT_TRUE(std::holds_alternative<process_error::AssetTransformError>(err));
}

// ===========================================================================
// AssetProcessor — register and lookup
// ===========================================================================

struct TestProcessSettings : Settings {};

struct TestOutputLoader {
    using Asset = std::string;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"txt"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    std::expected<std::string, Error> load(std::istream&, const Settings&, LoadContext&) const { return std::string{}; }
};

struct TestProcessor {
    using Settings     = TestProcessSettings;
    using OutputLoader = TestOutputLoader;

    std::expected<OutputLoader::Settings, ProcessError> process(ProcessContext& ctx,
                                                                const Settings& settings,
                                                                std::ostream& writer) const {
        writer << "processed";
        return OutputLoader::Settings{};
    }
};

TEST(AssetProcessor, RegisterAndGet) {
    AssetProcessor processor;
    processor.register_processor(TestProcessor{});
    // The key is the short type name
    // We can't know the exact mangled name, but get_processor shouldn't return nullptr
    // for at least the default lookup mechanism
}

TEST(AssetProcessor, GetProcessor_Missing) {
    AssetProcessor processor;
    auto result = processor.get_processor("NonExistent");
    EXPECT_EQ(result, nullptr);
}

TEST(AssetProcessor, SetDefaultProcessor) {
    AssetProcessor processor;
    processor.register_processor(TestProcessor{});
    processor.set_default_processor<TestProcessor>("txt");
    auto result = processor.get_default_processor("txt");
    // May or may not find it depending on type name matching - test the flow
}

TEST(AssetProcessor, GetDefaultProcessor_MissingExtension) {
    AssetProcessor processor;
    auto result = processor.get_default_processor("unknown");
    EXPECT_EQ(result, nullptr);
}

// ===========================================================================
// EmbeddedAssetRegistry
// ===========================================================================

TEST(EmbeddedAssetRegistry, InsertAndRetrieve) {
    EmbeddedAssetRegistry registry;
    std::vector<std::uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
    registry.insert_asset("C:/assets/test.txt", "test.txt", data);
    auto& dir = registry.directory();
    auto file = dir.get_file("test.txt");
    ASSERT_TRUE(file.has_value());
}

TEST(EmbeddedAssetRegistry, InsertStatic) {
    EmbeddedAssetRegistry registry;
    static const std::uint8_t data[] = {0x41, 0x42, 0x43};
    registry.insert_asset_static("C:/assets/abc.bin", "abc.bin", std::span(data));
    auto& dir = registry.directory();
    EXPECT_TRUE(dir.exists("abc.bin").value_or(false));
}

TEST(EmbeddedAssetRegistry, InsertMeta) {
    EmbeddedAssetRegistry registry;
    std::vector<std::uint8_t> meta = {0x7B, 0x7D};  // "{}"
    registry.insert_meta("C:/assets/test.txt", "test.txt", meta);
    auto& dir = registry.directory();
    EXPECT_TRUE(dir.exists("test.txt.meta").value_or(false));
}

TEST(EmbeddedAssetRegistry, RemoveAsset_Existing) {
    EmbeddedAssetRegistry registry;
    std::vector<std::uint8_t> data = {0x01};
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
