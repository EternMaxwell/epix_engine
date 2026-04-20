module;

#include <asio/awaitable.hpp>

export module epix.assets:io.reader;

import std;
import epix.meta;
import epix.utils;

namespace epix::assets {
export namespace reader_errors {
struct NotFound {
    std::filesystem::path path;
};
struct IoError {
    std::error_code code;
};
struct HttpError {
    int status;
};
}  // namespace reader_errors
export using AssetReaderError =
    std::variant<reader_errors::NotFound, reader_errors::IoError, reader_errors::HttpError, std::exception_ptr>;

/** @brief Abstract async byte reader. Matches bevy's Reader trait (AsyncRead + Unpin + Send + Sync).
 *  Concrete implementations: VecReader (in-memory), future FileReader, etc. */
export struct Reader {
    /** @brief Read all remaining bytes into buf, returning the number of bytes read.
     *  Matches bevy's Reader::read_to_end. */
    virtual asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) = 0;
    virtual ~Reader()                                                                                      = default;
};

/** @brief Abstract async byte writer. Matches bevy's Writer (dyn AsyncWrite + Unpin + Send + Sync). */
export struct Writer {
    /** @brief Write the given data, returning the number of bytes written. */
    virtual asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) = 0;
    /** @brief Flush any buffered output. */
    virtual asio::awaitable<std::expected<void, std::error_code>> flush() = 0;
    virtual ~Writer()                                                     = default;
};

export struct AssetReader {
    /** @brief Get an async reader for an asset. Matches bevy's AssetReader::read. */
    virtual asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> read(
        const std::filesystem::path& path) const = 0;
    /** @brief Get an async reader for meta data of an asset. Matches bevy's AssetReader::read_meta. */
    virtual asio::awaitable<std::expected<std::unique_ptr<Reader>, AssetReaderError>> read_meta(
        const std::filesystem::path& path) const = 0;
    /** @brief Get an iterator of directory entries. Matches bevy's AssetReader::read_directory. */
    virtual asio::awaitable<std::expected<epix::utils::input_iterable<std::filesystem::path>, AssetReaderError>>
    read_directory(const std::filesystem::path& path) const = 0;
    /** @brief Check if a path is a directory. Matches bevy's AssetReader::is_directory. */
    virtual asio::awaitable<std::expected<bool, AssetReaderError>> is_directory(
        const std::filesystem::path& path) const = 0;
    /** @brief Return the last-modified time of an asset, or nullopt if unsupported by this reader. */
    virtual std::optional<std::filesystem::file_time_type> last_modified(const std::filesystem::path& path) const {
        return std::nullopt;
    }
    /** @brief Read metadata bytes of an asset. Matches bevy's AssetReader::read_meta_bytes. */
    asio::awaitable<std::expected<std::vector<std::byte>, AssetReaderError>> read_meta_bytes(
        const std::filesystem::path& path) const;
    virtual ~AssetReader() = default;
};
export namespace writer_errors {
struct IoError {
    std::error_code code;
};
}  // namespace writer_errors
export using AssetWriterError = std::variant<writer_errors::IoError, std::exception_ptr>;
export struct AssetWriter {
    /** @brief Get an async writer for an asset. Matches bevy's AssetWriter::write. */
    virtual asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> write(
        const std::filesystem::path& path) const = 0;
    /** @brief Get an async writer for meta data of an asset. Matches bevy's AssetWriter::write_meta. */
    virtual asio::awaitable<std::expected<std::unique_ptr<Writer>, AssetWriterError>> write_meta(
        const std::filesystem::path& path) const = 0;
    /** @brief Removes the asset stored at the specified path. */
    virtual asio::awaitable<std::expected<void, AssetWriterError>> remove(const std::filesystem::path& path) const = 0;
    /** @brief Removes the meta data stored at the specified path. */
    virtual asio::awaitable<std::expected<void, AssetWriterError>> remove_meta(
        const std::filesystem::path& path) const = 0;
    /** @brief Renames the asset stored at `old_path` to `new_path`. */
    virtual asio::awaitable<std::expected<void, AssetWriterError>> rename(
        const std::filesystem::path& old_path, const std::filesystem::path& new_path) const = 0;
    /** @brief Renames the meta data stored at `old_path` to `new_path`. */
    virtual asio::awaitable<std::expected<void, AssetWriterError>> rename_meta(
        const std::filesystem::path& old_path, const std::filesystem::path& new_path) const = 0;
    virtual asio::awaitable<std::expected<void, AssetWriterError>> create_directory(
        const std::filesystem::path& path) const = 0;
    virtual asio::awaitable<std::expected<void, AssetWriterError>> remove_directory(
        const std::filesystem::path& path) const = 0;
    virtual asio::awaitable<std::expected<void, AssetWriterError>> clear_directory(
        const std::filesystem::path& path) const = 0;
    asio::awaitable<std::expected<void, AssetWriterError>> write_bytes(const std::filesystem::path& path,
                                                                       std::span<const std::byte> bytes) const;
    asio::awaitable<std::expected<void, AssetWriterError>> write_meta_bytes(const std::filesystem::path& path,
                                                                            std::span<const std::byte> bytes) const;
    virtual ~AssetWriter() = default;
};

export namespace source_events {
struct AddedAsset {
    std::filesystem::path path;
};
struct ModifiedAsset {
    std::filesystem::path path;
};
struct RemovedAsset {
    std::filesystem::path path;
};
struct RenamedAsset {
    std::filesystem::path old_path;
    std::filesystem::path new_path;
};
struct AddedMeta {
    std::filesystem::path path;
};
struct ModifiedMeta {
    std::filesystem::path path;
};
struct RemovedMeta {
    std::filesystem::path path;
};
struct RenamedMeta {
    std::filesystem::path old_path;
    std::filesystem::path new_path;
};
struct AddedDirectory {
    std::filesystem::path path;
};
struct RemovedDirectory {
    std::filesystem::path path;
};
struct RenamedDirectory {
    std::filesystem::path old_path;
    std::filesystem::path new_path;
};
struct RemovedUnknown {
    std::filesystem::path path;
    bool is_meta;
};

}  // namespace source_events
export using AssetSourceEvent = std::variant<source_events::AddedAsset,
                                             source_events::ModifiedAsset,
                                             source_events::RemovedAsset,
                                             source_events::RenamedAsset,
                                             source_events::AddedMeta,
                                             source_events::ModifiedMeta,
                                             source_events::RemovedMeta,
                                             source_events::RenamedMeta,
                                             source_events::AddedDirectory,
                                             source_events::RemovedDirectory,
                                             source_events::RenamedDirectory,
                                             source_events::RemovedUnknown>;

std::filesystem::path get_meta_path(const std::filesystem::path& asset_path) { return asset_path.string() + ".meta"; }

export struct AssetWatcher {
    virtual ~AssetWatcher() = default;
};

/** @brief An in-memory async reader backed by a byte vector.
 *  Matches bevy_asset's VecReader, which wraps a Vec<u8> into an AsyncRead stream.
 *  Implements the Reader interface for use with AssetLoader. */
export struct VecReader : Reader {
   private:
    std::vector<uint8_t> m_bytes;
    size_t m_bytes_read = 0;

   public:
    /** @brief Construct from a byte vector. */
    explicit VecReader(std::vector<uint8_t> bytes) : m_bytes(std::move(bytes)) {}
    /** @brief Construct from a string. */
    explicit VecReader(std::string data) : m_bytes(data.begin(), data.end()) {}
    /** @brief Construct from a string_view. */
    explicit VecReader(std::string_view data) : m_bytes(data.begin(), data.end()) {}

    VecReader(const VecReader&)            = delete;
    VecReader& operator=(const VecReader&) = delete;
    VecReader(VecReader&&)                 = default;
    VecReader& operator=(VecReader&&)      = default;

    asio::awaitable<std::expected<size_t, std::error_code>> read_to_end(std::vector<uint8_t>& buf) override;

    /** @brief Get a view of the underlying bytes. */
    std::span<const uint8_t> bytes() const { return m_bytes; }
};

/** @brief An in-memory async writer backed by a byte vector.
 *  Matches bevy's pattern of writing to a buffer.
 *  Implements the Writer interface for use with AssetSaver. */
export struct VecWriter : Writer {
   private:
    std::vector<uint8_t> m_data;

   public:
    asio::awaitable<std::expected<size_t, std::error_code>> write(std::span<const uint8_t> data) override;
    asio::awaitable<std::expected<void, std::error_code>> flush() override;

    /** @brief Get the written bytes. */
    std::vector<uint8_t>& bytes() { return m_data; }
    const std::vector<uint8_t>& bytes() const { return m_data; }
};

}  // namespace epix::assets