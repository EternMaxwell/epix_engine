module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <ranges>
#include <span>
#include <system_error>
#include <utility>
#include <vector>
#endif
#include <asio/awaitable.hpp>
#include <asio/detail/config.hpp>
#ifdef ASIO_HAS_FILE
#include <asio/buffer.hpp>
#include <asio/read.hpp>
#include <asio/stream_file.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#endif  // ASIO_HAS_FILE

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

namespace epix::assets {

#ifdef ASIO_HAS_FILE

// ---- io_uring path: true async file I/O, zero intermediate copies ----

struct FileReader : Reader {
    asio::stream_file m_file;

    explicit FileReader(asio::stream_file file) : m_file(std::move(file)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto initial_size = buf.size();
        try {
            co_await asio::async_read(m_file, asio::dynamic_buffer(buf), asio::use_awaitable);
        } catch (const std::system_error& e) {
            if (e.code() != asio::error::eof) {
                co_return std::unexpected(e.code());
            }
        }
        co_return buf.size() - initial_size;
    }
};

struct FileWriter : Writer {
    asio::stream_file m_file;

    explicit FileWriter(asio::stream_file file) : m_file(std::move(file)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        try {
            std::size_t n =
                co_await asio::async_write(m_file, asio::buffer(data.data(), data.size()), asio::use_awaitable);
            co_return n;
        } catch (const std::system_error& e) {
            co_return std::unexpected(e.code());
        }
    }

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        // stream_file bypasses userspace buffering; writes go directly to the OS.
        co_return std::expected<void, std::error_code>{};
    }
};

#else  // ASIO_HAS_FILE

// ---- Fallback: synchronous reads/writes directly into the caller's buffer ----
// Single copy (file → buf) for reads; no intermediate vector.

struct FileReader : Reader {
    std::ifstream m_stream;
    std::size_t m_file_size;

    explicit FileReader(std::ifstream stream, std::size_t file_size)
        : m_stream(std::move(stream)), m_file_size(file_size) {}

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        const auto offset = buf.size();
        buf.resize(offset + m_file_size);
        if (!m_stream.read(reinterpret_cast<char*>(buf.data() + offset), static_cast<std::streamsize>(m_file_size))) {
            buf.resize(offset);
            co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        }
        co_return m_file_size;
    }
};

struct FileWriter : Writer {
    std::ofstream m_stream;

    explicit FileWriter(std::ofstream stream) : m_stream(std::move(stream)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        m_stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!m_stream) co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        co_return data.size();
    }

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        m_stream.flush();
        if (!m_stream) co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        co_return std::expected<void, std::error_code>{};
    }
};

#endif  // ASIO_HAS_FILE

// ---- FileAssetReader ----------------------------------------------------

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
#ifdef ASIO_HAS_FILE
        auto executor = co_await asio::this_coro::executor;
        asio::stream_file file(executor, full_path.string(), asio::stream_file::read_only);
        co_return std::unique_ptr<Reader>(std::make_unique<FileReader>(std::move(file)));
#else
        std::ifstream stream(full_path, std::ios::binary | std::ios::ate);
        if (!stream.is_open()) {
            co_return std::unexpected(
                AssetReaderError(reader_errors::IoError{std::make_error_code(std::io_errc::stream)}));
        }
        const auto file_size = static_cast<std::size_t>(stream.tellg());
        stream.seekg(0, std::ios::beg);
        co_return std::unique_ptr<Reader>(std::make_unique<FileReader>(std::move(stream), file_size));
#endif
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read_meta(
    const std::filesystem::path& path) const {
    co_return co_await read(get_meta_path(path));
}

asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>>
FileAssetReader::read_directory(const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
        co_return std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>(
            std::filesystem::directory_iterator(full_path) |
            std::views::transform(
                [rel = path](const std::filesystem::directory_entry& e) { return rel / e.path().filename(); }));
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

asio::awaitable<std::expected<bool, AssetReaderError>> FileAssetReader::is_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
        co_return std::expected<bool, AssetReaderError>(std::filesystem::is_directory(full_path));
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetReaderError(reader_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

// ---- FileAssetWriter ----------------------------------------------------

asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> FileAssetWriter::write(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        std::filesystem::create_directories(full_path.parent_path());
#ifdef ASIO_HAS_FILE
        auto executor = co_await asio::this_coro::executor;
        asio::stream_file file(executor, full_path.string(),
                               asio::stream_file::write_only | asio::stream_file::create | asio::stream_file::truncate);
        co_return std::unique_ptr<Writer>(std::make_unique<FileWriter>(std::move(file)));
#else
        std::ofstream stream(full_path, std::ios::binary);
        if (!stream.is_open()) {
            co_return std::unexpected(
                AssetWriterError(writer_errors::IoError{std::make_error_code(std::io_errc::stream)}));
        }
        co_return std::unique_ptr<Writer>(std::make_unique<FileWriter>(std::move(stream)));
#endif
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> FileAssetWriter::write_meta(
    const std::filesystem::path& path) const {
    co_return co_await write(get_meta_path(path));
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::remove(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
            std::filesystem::remove(full_path);
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::remove_meta(
    const std::filesystem::path& path) const {
    co_return co_await remove(get_meta_path(path));
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::rename(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    try {
        auto full_old_path = m_root / old_path;
        auto full_new_path = m_root / new_path;
        if (std::filesystem::exists(full_old_path)) {
            std::filesystem::create_directories(full_new_path.parent_path());
            std::filesystem::rename(full_old_path, full_new_path);
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::rename_meta(
    const std::filesystem::path& old_path, const std::filesystem::path& new_path) const {
    co_return co_await rename(get_meta_path(old_path), get_meta_path(new_path));
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::create_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        std::filesystem::create_directories(full_path);
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::remove_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_directory(full_path)) {
            std::filesystem::remove_all(full_path);
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

asio::awaitable<std::expected<void, AssetWriterError>> FileAssetWriter::clear_directory(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_directory(full_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                std::filesystem::remove_all(entry.path());
            }
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (const std::system_error& e) {
        co_return std::unexpected(AssetWriterError(writer_errors::IoError{e.code()}));
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

}  // namespace epix::assets
