module;

export module epix.assets:io.processor_gated;

import std;
import epix.utils;

import :path;
import :io.reader;
import :processor;

namespace assets {

/// A stream wrapper that holds a shared transaction lock, preventing the processor
/// from writing while the stream is being read.
struct TransactionLockedStream : public std::istream {
    std::unique_ptr<std::istream> m_inner;
    std::shared_ptr<std::shared_mutex> m_mutex;  // prevent mutex destruction
    std::shared_lock<std::shared_mutex> m_lock;

    TransactionLockedStream(std::unique_ptr<std::istream> inner,
                            std::shared_ptr<std::shared_mutex> mutex,
                            std::shared_lock<std::shared_mutex> lock)
        : std::istream(inner->rdbuf()), m_inner(std::move(inner)), m_mutex(std::move(mutex)), m_lock(std::move(lock)) {}
};

/// An AssetReader that will prevent asset (and asset metadata) reads from returning
/// for a given path until that path has been processed by AssetProcessor.
/// The inner reader is borrowed (not owned) — the caller must ensure it outlives this object.
export struct ProcessorGatedReader : public AssetReader {
   private:
    const AssetReader* m_reader;  // borrowed, must outlive this
    AssetSourceId m_source;
    std::shared_ptr<ProcessingState> m_processing_state;

   public:
    ProcessorGatedReader(AssetSourceId source,
                         const AssetReader& reader,
                         std::shared_ptr<ProcessingState> processing_state)
        : m_source(std::move(source)), m_reader(&reader), m_processing_state(std::move(processing_state)) {}

    std::expected<std::unique_ptr<std::istream>, AssetReaderError> read(
        const std::filesystem::path& path) const override {
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

    std::expected<std::unique_ptr<std::istream>, AssetReaderError> read_meta(
        const std::filesystem::path& path) const override {
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

    std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError> read_directory(
        const std::filesystem::path& path) const override {
        m_processing_state->wait_until_finished();
        return m_reader->read_directory(path);
    }

    std::expected<bool, AssetReaderError> is_directory(const std::filesystem::path& path) const override {
        m_processing_state->wait_until_finished();
        return m_reader->is_directory(path);
    }
};

}  // namespace assets
