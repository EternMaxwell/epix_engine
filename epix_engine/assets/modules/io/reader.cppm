module;

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

// TODO: use coroutines async operation for readers, and return a future/awaitable instead of blocking the thread

export struct AssetReader {
    /** @brief Get a stream to read an asset */
    virtual std::expected<std::unique_ptr<std::istream>, AssetReaderError> read(
        const std::filesystem::path& path) const = 0;
    /** @brief Get a stream to read meta data of an asset */
    virtual std::expected<std::unique_ptr<std::istream>, AssetReaderError> read_meta(
        const std::filesystem::path& path) const = 0;
    /** @brief Get an iterator of directory entries */
    virtual std::expected<epix::utils::input_iterable<std::filesystem::path>, AssetReaderError> read_directory(
        const std::filesystem::path& path) const = 0;
    /** @brief Check if a path is a directory */
    virtual std::expected<bool, AssetReaderError> is_directory(const std::filesystem::path& path) const = 0;
    /** @brief Return the last-modified time of an asset, or nullopt if unsupported by this reader. */
    virtual std::optional<std::filesystem::file_time_type> last_modified(const std::filesystem::path& path) const {
        return std::nullopt;
    }
    /** @brief Read metadata bytes of an asset */
    std::expected<std::vector<std::byte>, AssetReaderError> read_meta_bytes(const std::filesystem::path& path) const;
    virtual ~AssetReader() = default;
};
export namespace writer_errors {
struct IoError {
    std::error_code code;
};
}  // namespace writer_errors
export using AssetWriterError = std::variant<writer_errors::IoError, std::exception_ptr>;
export struct AssetWriter {
    /** @brief Get a stream to write an asset */
    virtual std::expected<std::unique_ptr<std::ostream>, AssetWriterError> write(
        const std::filesystem::path& path) const = 0;
    /** @brief Get a stream to write meta data of an asset.
     * The path should not include storage specific extensions like `.meta` */
    virtual std::expected<std::unique_ptr<std::ostream>, AssetWriterError> write_meta(
        const std::filesystem::path& path) const = 0;
    /** @brief Removes the asset stored at the specified path */
    virtual std::expected<void, AssetWriterError> remove(const std::filesystem::path& path) const = 0;
    /** @brief Removes the meta data stored at the specified path */
    virtual std::expected<void, AssetWriterError> remove_meta(const std::filesystem::path& path) const = 0;
    /** @brief Renames the asset stored at `old_path` to `new_path` */
    virtual std::expected<void, AssetWriterError> rename(const std::filesystem::path& old_path,
                                                         const std::filesystem::path& new_path) const = 0;
    /* @brief Renames the meta data stored at `old_path` to `new_path` */
    virtual std::expected<void, AssetWriterError> rename_meta(const std::filesystem::path& old_path,
                                                              const std::filesystem::path& new_path) const  = 0;
    virtual std::expected<void, AssetWriterError> create_directory(const std::filesystem::path& path) const = 0;
    virtual std::expected<void, AssetWriterError> remove_directory(const std::filesystem::path& path) const = 0;
    virtual std::expected<void, AssetWriterError> clear_directory(const std::filesystem::path& path) const  = 0;
    std::expected<void, AssetWriterError> write_bytes(const std::filesystem::path& path,
                                                      std::span<const std::byte> bytes) const;
    std::expected<void, AssetWriterError> write_meta_bytes(const std::filesystem::path& path,
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

/** @brief An in-memory reader backed by a byte vector.
 *  Matches bevy_asset's VecReader, which wraps a Vec<u8> into an AsyncRead stream.
 *  C++ equivalent: an istream backed by an in-memory byte buffer.
 *  Usage: pass the returned stream as the std::istream& argument to asset loaders. */
export struct VecReader {
   private:
    std::string m_data;
    std::istringstream m_stream;

   public:
    /** @brief Construct from a byte vector. */
    explicit VecReader(std::vector<uint8_t> bytes)
        : m_data(reinterpret_cast<const char*>(bytes.data()), bytes.size()), m_stream(m_data) {}
    /** @brief Construct from a string. */
    explicit VecReader(std::string data) : m_data(std::move(data)), m_stream(m_data) {}
    /** @brief Construct from a string_view. */
    explicit VecReader(std::string_view data) : m_data(data), m_stream(m_data) {}

    VecReader(const VecReader&)            = delete;
    VecReader& operator=(const VecReader&) = delete;
    VecReader(VecReader&&)                 = delete;
    VecReader& operator=(VecReader&&)      = delete;

    /** @brief Get the underlying istream. Pass this to asset loaders. */
    std::istream& stream() { return m_stream; }
    /** @brief Implicit conversion to std::istream& for use as loader argument. */
    operator std::istream&() { return m_stream; }
};

}  // namespace epix::assets