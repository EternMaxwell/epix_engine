module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <shared_mutex>
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

// Wraps a Reader with a shared transaction lock, ensuring the lock
// is held for the lifetime of the reader.
struct TransactionLockedReader : Reader {
    std::unique_ptr<Reader> m_reader;
    std::shared_ptr<std::shared_mutex> m_mutex;
    std::shared_lock<std::shared_mutex> m_lock;

    TransactionLockedReader(std::unique_ptr<Reader> r,
                            std::shared_ptr<std::shared_mutex> m,
                            std::shared_lock<std::shared_mutex> l)
        : m_reader(std::move(r)), m_mutex(std::move(m)), m_lock(std::move(l)) {}

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override {
        co_return co_await m_reader->read_to_end(buf);
    }
};

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> ProcessorGatedReader::read(
    const std::filesystem::path& path) const {
    auto asset_path = AssetPath(m_source, path);
    auto status     = m_processing_state->wait_until_processed(asset_path);
    if (status != ProcessStatus::Processed) {
        co_return std::unexpected(AssetReaderError(reader_errors::NotFound{path}));
    }
    auto lock_result = m_processing_state->get_transaction_lock(asset_path);
    if (!lock_result) co_return std::unexpected(lock_result.error());
    auto mutex       = lock_result.value();
    auto lock        = std::shared_lock(*mutex);
    auto read_result = co_await m_reader->read(path);
    if (!read_result) co_return std::unexpected(read_result.error());
    co_return std::make_unique<TransactionLockedReader>(std::move(*read_result), std::move(mutex), std::move(lock));
}

asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> ProcessorGatedReader::read_meta(
    const std::filesystem::path& path) const {
    auto asset_path = AssetPath(m_source, path);
    auto status     = m_processing_state->wait_until_processed(asset_path);
    if (status != ProcessStatus::Processed) {
        co_return std::unexpected(AssetReaderError(reader_errors::NotFound{path}));
    }
    auto lock_result = m_processing_state->get_transaction_lock(asset_path);
    if (!lock_result) co_return std::unexpected(lock_result.error());
    auto mutex       = lock_result.value();
    auto lock        = std::shared_lock(*mutex);
    auto read_result = co_await m_reader->read_meta(path);
    if (!read_result) co_return std::unexpected(read_result.error());
    co_return std::make_unique<TransactionLockedReader>(std::move(*read_result), std::move(mutex), std::move(lock));
}

asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>>
ProcessorGatedReader::read_directory(const std::filesystem::path& path) const {
    m_processing_state->wait_until_finished();
    co_return co_await m_reader->read_directory(path);
}

asio::awaitable<std::expected<bool, AssetReaderError>> ProcessorGatedReader::is_directory(
    const std::filesystem::path& path) const {
    m_processing_state->wait_until_finished();
    co_return co_await m_reader->is_directory(path);
}

}  // namespace epix::assets
