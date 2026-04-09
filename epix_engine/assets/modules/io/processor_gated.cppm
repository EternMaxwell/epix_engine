module;

export module epix.assets:io.processor_gated;

import std;
import epix.utils;

import :path;
import :io.reader;
import :processor;

namespace epix::assets {

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
struct ProcessorGatedReader : public AssetReader {
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
        const std::filesystem::path& path) const override;

    std::expected<std::unique_ptr<std::istream>, AssetReaderError> read_meta(
        const std::filesystem::path& path) const override;

    std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError> read_directory(
        const std::filesystem::path& path) const override;

    std::expected<bool, AssetReaderError> is_directory(const std::filesystem::path& path) const override;
};

}  // namespace epix::assets
