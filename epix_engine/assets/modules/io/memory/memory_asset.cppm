module;

#include <asio/awaitable.hpp>

export module epix.assets:io.memory.asset;

import std;
import epix.meta;
import epix.utils;

import :io.reader;
import :io.memory;

namespace epix::assets {

// helper: convert DirectoryError variant to AssetReaderError
static AssetReaderError dir_error_to_reader_error(const memory::DirectoryError& derr,
                                                  const std::filesystem::path& fallback_path = {});

// helper: convert DirectoryError variant to AssetWriterError
static AssetWriterError dir_error_to_writer_error(const memory::DirectoryError& derr);

// Implement polymorphic adapters that satisfy the engine AssetReader/AssetWriter/AssetWatcher
export struct MemoryAssetReader : public assets::AssetReader {
   public:
    explicit MemoryAssetReader(assets::memory::Directory dir) : dir_(std::move(dir)) {}

    asio::awaitable<std::expected<std::unique_ptr<Reader>, assets::AssetReaderError>> read(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<std::unique_ptr<Reader>, assets::AssetReaderError>> read_meta(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, assets::AssetReaderError>>
    read_directory(const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<bool, assets::AssetReaderError>> is_directory(
        const std::filesystem::path& path) const override;

   private:
    assets::memory::Directory dir_;
};

export struct MemoryAssetWriter : public assets::AssetWriter {
   public:
    explicit MemoryAssetWriter(assets::memory::Directory dir) : dir_(std::move(dir)) {}

    asio::awaitable<std::expected<std::unique_ptr<Writer>, assets::AssetWriterError>> write(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<std::unique_ptr<Writer>, assets::AssetWriterError>> write_meta(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> remove(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> remove_meta(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> rename(
        const std::filesystem::path& old_path, const std::filesystem::path& new_path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> rename_meta(
        const std::filesystem::path& old_path, const std::filesystem::path& new_path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> create_directory(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> remove_directory(
        const std::filesystem::path& path) const override;

    asio::awaitable<std::expected<void, assets::AssetWriterError>> clear_directory(
        const std::filesystem::path& path) const override;

   private:
    assets::memory::Directory dir_;
};

export struct MemoryAssetWatcher : public assets::AssetWatcher {
   public:
    // callback receives AssetSourceEvent
    MemoryAssetWatcher(assets::memory::Directory dir, std::function<void(assets::AssetSourceEvent)> cb);
    ~MemoryAssetWatcher() { dir_.remove_callback(sub_id_); }

   private:
    assets::AssetSourceEvent convert(const assets::memory::DirEvent& ev);

    assets::memory::Directory dir_;
    std::function<void(assets::AssetSourceEvent)> cb_;
    std::uint64_t sub_id_ = 0;
};

}  // namespace epix::assets