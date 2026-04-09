module;
module epix.assets;

import std;
import epix.utils;

namespace epix::assets {

std::expected<std::unique_ptr<std::istream>, AssetReaderError> ProcessorGatedReader::read(
    const std::filesystem::path& path) const {
    auto asset_path = AssetPath(m_source, path);
    auto status     = m_processing_state->wait_until_processed(asset_path);
    if (status != ProcessStatus::Processed) {
        return std::unexpected(AssetReaderError(reader_errors::NotFound{path}));
    }
    auto lock_result = m_processing_state->get_transaction_lock(asset_path);
    if (!lock_result) return std::unexpected(lock_result.error());
    auto mutex       = lock_result.value();
    auto lock        = std::shared_lock(*mutex);
    auto read_result = m_reader->read(path);
    if (!read_result) return std::unexpected(read_result.error());
    return std::make_unique<TransactionLockedStream>(std::move(*read_result), std::move(mutex), std::move(lock));
}

std::expected<std::unique_ptr<std::istream>, AssetReaderError> ProcessorGatedReader::read_meta(
    const std::filesystem::path& path) const {
    auto asset_path = AssetPath(m_source, path);
    auto status     = m_processing_state->wait_until_processed(asset_path);
    if (status != ProcessStatus::Processed) {
        return std::unexpected(AssetReaderError(reader_errors::NotFound{path}));
    }
    auto lock_result = m_processing_state->get_transaction_lock(asset_path);
    if (!lock_result) return std::unexpected(lock_result.error());
    auto mutex       = lock_result.value();
    auto lock        = std::shared_lock(*mutex);
    auto read_result = m_reader->read_meta(path);
    if (!read_result) return std::unexpected(read_result.error());
    return std::make_unique<TransactionLockedStream>(std::move(*read_result), std::move(mutex), std::move(lock));
}

std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError> ProcessorGatedReader::read_directory(
    const std::filesystem::path& path) const {
    m_processing_state->wait_until_finished();
    return m_reader->read_directory(path);
}

std::expected<bool, AssetReaderError> ProcessorGatedReader::is_directory(const std::filesystem::path& path) const {
    m_processing_state->wait_until_finished();
    return m_reader->is_directory(path);
}

}  // namespace epix::assets
