module;

#include <asio/awaitable.hpp>

export module epix.assets:io.processor_gated;

import std;
import epix.utils;

import :path;
import :io.reader;
import :processor;

namespace epix::assets {

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

    asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> read(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> read_meta(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>> read_directory(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<bool, AssetReaderError>> is_directory(
        const std::filesystem::path& path) const override;
};

}  // namespace epix::assets
