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

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

namespace epix::assets {

// ---- File-backed Reader (reads entire file into memory on read_to_end) ----

struct FileReader : Reader {
    std::vector<uint8_t> m_data;
    bool m_consumed = false;

    explicit FileReader(std::vector<uint8_t> data) : m_data(std::move(data)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        if (m_consumed) co_return size_t{0};
        buf.insert(buf.end(), m_data.begin(), m_data.end());
        m_consumed = true;
        co_return m_data.size();
    }
};

// ---- File-backed Writer (writes to ofstream) ----

struct FileWriter : Writer {
    std::ofstream m_stream;

    explicit FileWriter(std::ofstream stream) : m_stream(std::move(stream)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override {
        m_stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!m_stream) {
            co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        }
        co_return data.size();
    }

    asio::awaitable<std::expected<void, std::error_code>> flush() override {
        m_stream.flush();
        if (!m_stream) {
            co_return std::unexpected(std::make_error_code(std::io_errc::stream));
        }
        co_return std::expected<void, std::error_code>{};
    }
};

// ---- FileAssetReader ----------------------------------------------------

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> FileAssetReader::read(
    const std::filesystem::path& path) const {
    try {
        auto full_path = m_root / path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            co_return std::unexpected(AssetReaderError(reader_errors::NotFound{full_path}));
        }
        std::ifstream stream(full_path, std::ios::binary);
        if (!stream.is_open()) {
            co_return std::unexpected(
                AssetReaderError(reader_errors::IoError{std::make_error_code(std::io_errc::stream)}));
        }
        // Read entire file into memory
        std::vector<uint8_t> data(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>{});
        co_return std::unique_ptr<Reader>(std::make_unique<FileReader>(std::move(data)));
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
        std::ofstream stream(full_path, std::ios::binary);
        if (!stream.is_open()) {
            co_return std::unexpected(
                AssetWriterError(writer_errors::IoError{std::make_error_code(std::io_errc::stream)}));
        }
        co_return std::unique_ptr<Writer>(std::make_unique<FileWriter>(std::move(stream)));
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
