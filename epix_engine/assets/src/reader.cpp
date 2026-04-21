module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>
#endif
#include <asio/awaitable.hpp>

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::assets {

asio::awaitable<std::expected<std::vector<std::byte>, AssetReaderError>> AssetReader::read_meta_bytes(
    const std::filesystem::path& path) const {
    auto reader_result = co_await read_meta(path);
    if (!reader_result) {
        co_return std::unexpected(reader_result.error());
    }
    auto& reader = *reader_result;
    try {
        std::vector<uint8_t> buf;
        auto read_result = co_await reader->read_to_end(buf);
        if (!read_result) {
            co_return std::unexpected(AssetReaderError(reader_errors::IoError{read_result.error()}));
        }
        std::vector<std::byte> bytes(buf.size());
        std::memcpy(bytes.data(), buf.data(), buf.size());
        co_return bytes;
    } catch (...) {
        co_return std::unexpected(AssetReaderError(std::current_exception()));
    }
}

asio::awaitable<std::expected<void, AssetWriterError>> AssetWriter::write_bytes(
    const std::filesystem::path& path, std::span<const std::byte> bytes) const {
    auto writer_result = co_await write(path);
    if (!writer_result) {
        co_return std::unexpected(writer_result.error());
    }
    auto& writer = *writer_result;
    try {
        auto data         = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        auto write_result = co_await writer->write(data);
        if (!write_result) {
            co_return std::unexpected(AssetWriterError(writer_errors::IoError{write_result.error()}));
        }
        auto flush_result = co_await writer->flush();
        if (!flush_result) {
            co_return std::unexpected(AssetWriterError(writer_errors::IoError{flush_result.error()}));
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

asio::awaitable<std::expected<void, AssetWriterError>> AssetWriter::write_meta_bytes(
    const std::filesystem::path& path, std::span<const std::byte> bytes) const {
    auto writer_result = co_await write_meta(path);
    if (!writer_result) {
        co_return std::unexpected(writer_result.error());
    }
    auto& writer = *writer_result;
    try {
        auto data         = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        auto write_result = co_await writer->write(data);
        if (!write_result) {
            co_return std::unexpected(AssetWriterError(writer_errors::IoError{write_result.error()}));
        }
        auto flush_result = co_await writer->flush();
        if (!flush_result) {
            co_return std::unexpected(AssetWriterError(writer_errors::IoError{flush_result.error()}));
        }
        co_return std::expected<void, AssetWriterError>{};
    } catch (...) {
        co_return std::unexpected(AssetWriterError(std::current_exception()));
    }
}

// --- VecReader implementation ---

asio::awaitable<std::expected<size_t, std::error_code>> VecReader::read_to_end(std::vector<uint8_t>& buf) {
    if (m_bytes_read >= m_bytes.size()) {
        co_return size_t{0};
    }
    auto remaining = std::span<const uint8_t>(m_bytes).subspan(m_bytes_read);
    buf.insert(buf.end(), remaining.begin(), remaining.end());
    auto len     = remaining.size();
    m_bytes_read = m_bytes.size();
    co_return len;
}

// --- VecWriter implementation ---

asio::awaitable<std::expected<size_t, std::error_code>> VecWriter::write(std::span<const uint8_t> data) {
    m_data.insert(m_data.end(), data.begin(), data.end());
    co_return data.size();
}

asio::awaitable<std::expected<void, std::error_code>> VecWriter::flush() {
    co_return std::expected<void, std::error_code>{};
}

}  // namespace epix::assets
