module;

#include <asio/awaitable.hpp>

export module epix.assets:io.file.asset;

import std;
import epix.meta;
import epix.utils;

import :io.reader;

namespace epix::assets {
export struct FileAssetReader : public AssetReader {
   private:
    std::filesystem::path m_root;

   public:
    FileAssetReader(std::filesystem::path root) : m_root(std::filesystem::absolute(std::move(root))) {}
    asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> read(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> read_meta(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<utils::input_iterable<std::filesystem::path>, AssetReaderError>> read_directory(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<bool, AssetReaderError>> is_directory(
        const std::filesystem::path& path) const override;
    std::optional<std::filesystem::file_time_type> last_modified(const std::filesystem::path& path) const override {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(m_root / path, ec);
        if (ec) return std::nullopt;
        return t;
    }
};
static_assert(!std::is_abstract_v<FileAssetReader>);

export struct FileAssetWriter : public AssetWriter {
   private:
    std::filesystem::path m_root;

   public:
    FileAssetWriter(std::filesystem::path root) : m_root(std::filesystem::absolute(std::move(root))) {}
    asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> write(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> write_meta(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> remove(const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> remove_meta(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> rename(const std::filesystem::path& old_path,
                                                                  const std::filesystem::path& new_path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> rename_meta(
        const std::filesystem::path& old_path, const std::filesystem::path& new_path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> create_directory(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> remove_directory(
        const std::filesystem::path& path) const override;
    asio::awaitable<std::expected<void, AssetWriterError>> clear_directory(
        const std::filesystem::path& path) const override;
};
static_assert(!std::is_abstract_v<FileAssetWriter>);
}  // namespace epix::assets