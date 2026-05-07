#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.assets;

using namespace epix::assets;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Run an awaitable on a fresh io_context and return its result.
template <typename T>
static T run(asio::awaitable<T> coro) {
    std::optional<T> result;
    asio::io_context ctx;
    asio::co_spawn(
        ctx,
        [coro = std::move(coro), &result]() mutable -> asio::awaitable<void> { result = co_await std::move(coro); },
        asio::detached);
    ctx.run();
    return std::move(*result);
}

// Root used by all FileAssetReader/Writer under test.
// Tests run with WORKING_DIRECTORY = CMAKE_SOURCE_DIR (engine root),
// so "assets/tests" resolves to <engine_root>/assets/tests/.
static const std::filesystem::path kRoot{"assets/tests/file_io"};

// ---------------------------------------------------------------------------
// FileAssetReader tests
// ---------------------------------------------------------------------------

TEST(FileAssetReader, ReadExistingFile) {
    FileAssetReader reader(kRoot);

    std::vector<uint8_t> buf;
    auto result = run([&]() -> asio::awaitable<bool> {
        auto r = co_await reader.read("hello.txt");
        if (!r) co_return false;
        auto res = co_await (*r)->read_to_end(buf);
        co_return res.has_value();
    }());

    ASSERT_TRUE(result);
    ASSERT_FALSE(buf.empty());
    std::string content(buf.begin(), buf.end());
    EXPECT_NE(content.find("hello"), std::string::npos);
}

TEST(FileAssetReader, ReadMissingFileReturnsNotFound) {
    FileAssetReader reader(kRoot);

    auto result = run([&]() -> asio::awaitable<bool> {
        auto r = co_await reader.read("no_such_file.txt");
        if (r) co_return false;
        co_return std::holds_alternative<reader_errors::NotFound>(r.error());
    }());

    EXPECT_TRUE(result);
}

TEST(FileAssetReader, IsDirectoryOnDir) {
    FileAssetReader reader(kRoot);
    auto result = run(reader.is_directory(""));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
}

TEST(FileAssetReader, IsDirectoryOnFile) {
    FileAssetReader reader(kRoot);
    auto result = run(reader.is_directory("hello.txt"));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
}

// ---------------------------------------------------------------------------
// FileAssetWriter tests
// ---------------------------------------------------------------------------

// Helper: clean up a file after the test.
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

TEST(FileAssetWriter, WriteAndReadBack) {
    FileAssetWriter writer(kRoot);
    FileAssetReader reader(kRoot);
    TempFile tf(kRoot / "write_test.bin");

    const std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0xFF};

    bool write_ok = run([&]() -> asio::awaitable<bool> {
        auto w = co_await writer.write("write_test.bin");
        if (!w) co_return false;
        auto res = co_await (*w)->write(std::span<const uint8_t>(data));
        co_return res.has_value() && *res == data.size();
    }());
    ASSERT_TRUE(write_ok);

    std::vector<uint8_t> buf;
    bool read_ok = run([&]() -> asio::awaitable<bool> {
        auto r = co_await reader.read("write_test.bin");
        if (!r) co_return false;
        auto res = co_await (*r)->read_to_end(buf);
        co_return res.has_value();
    }());
    ASSERT_TRUE(read_ok);
    EXPECT_EQ(buf, data);
}

TEST(FileAssetWriter, RemoveExistingFile) {
    FileAssetWriter writer(kRoot);

    // Create a file first.
    {
        std::ofstream f(kRoot / "remove_test.bin", std::ios::binary);
        f << "x";
    }
    ASSERT_TRUE(std::filesystem::exists(kRoot / "remove_test.bin"));

    bool ok = run([&]() -> asio::awaitable<bool> {
        auto res = co_await writer.remove("remove_test.bin");
        co_return res.has_value();
    }());
    EXPECT_TRUE(ok);
    EXPECT_FALSE(std::filesystem::exists(kRoot / "remove_test.bin"));
}
