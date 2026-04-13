# I/O — Asset Reader, Writer, and Watcher

Abstract interfaces for reading and writing asset bytes from a source, and watching for changes.

```cpp
#import epix.assets
```

---

## `AssetReader`

Abstract base for reading asset bytes from a source.

```cpp
struct AssetReader {
    // Open a stream for the asset bytes at path
    virtual std::expected<std::unique_ptr<std::istream>, AssetReaderError>
        read(const std::filesystem::path& path) const = 0;

    // Open a stream for the sidecar .meta file
    virtual std::expected<std::unique_ptr<std::istream>, AssetReaderError>
        read_meta(const std::filesystem::path& path) const = 0;

    // Read the sidecar .meta file into a byte buffer
    std::expected<std::vector<std::byte>, AssetReaderError>
        read_meta_bytes(const std::filesystem::path& path) const;

    // List all entries in a directory (non-recursive)
    virtual std::expected<epix::utils::input_iterable<std::filesystem::path>, AssetReaderError>
        read_directory(const std::filesystem::path& path) const = 0;

    // Test whether path refers to a directory
    virtual std::expected<bool, AssetReaderError>
        is_directory(const std::filesystem::path& path) const = 0;

    // Return the source file's last-modified time, or nullopt if unsupported.
    // Used by the processor pipeline for mtime-based skip optimisation.
    virtual std::optional<std::filesystem::file_time_type>
        last_modified(const std::filesystem::path& path) const;
        // default implementation returns std::nullopt
};
```

`read_meta_bytes()` is a non-virtual convenience method built on top of `read_meta()`.

`last_modified()` is optional — the default returns `nullopt`. `FileAssetReader` overrides it
with `std::filesystem::last_write_time`. Readers that cannot report timestamps (e.g.
`EmbeddedAssetReader`, `MemoryAssetReader`) leave the default. When `nullopt`, the processor
pipeline falls back to BLAKE3 hash comparison to detect changes.

---

## `AssetWriter`

Abstract base for writing processed asset bytes.

```cpp
struct AssetWriter {
    virtual std::expected<std::unique_ptr<std::ostream>, AssetWriterError>
        write(const std::filesystem::path& path) = 0;

    virtual std::expected<std::unique_ptr<std::ostream>, AssetWriterError>
        write_meta(const std::filesystem::path& path) = 0;

    virtual std::expected<void, AssetWriterError>
        remove(const std::filesystem::path& path) = 0;

    virtual std::expected<void, AssetWriterError>
        remove_meta(const std::filesystem::path& path) = 0;

    virtual std::expected<void, AssetWriterError>
        rename(const std::filesystem::path& from,
               const std::filesystem::path& to) = 0;

    virtual std::expected<void, AssetWriterError>
        rename_meta(const std::filesystem::path& from,
                    const std::filesystem::path& to) = 0;

    virtual std::expected<void, AssetWriterError>
        create_directory(const std::filesystem::path& path) = 0;

    virtual std::expected<void, AssetWriterError>
        remove_directory(const std::filesystem::path& path) = 0;

    virtual std::expected<void, AssetWriterError>
        clear_directory(const std::filesystem::path& path) = 0;
};
```

---

## `AssetWatcher`

Abstract base for asset source change watchers.

```cpp
struct AssetWatcher {
    virtual ~AssetWatcher() = default;
};
```

`AssetWatcher` is a marker base class. Concrete implementations (e.g. `FileAssetWatcher`,
`MemoryAssetWatcher`) provide their own event-draining interfaces via the `AssetSource`
event-receiver mechanism.

---

## `AssetSourceEvent`

A change event from the watcher. Variant of all event kinds.

```cpp
namespace source_events {
    struct AddedAsset      { std::filesystem::path path; };
    struct ModifiedAsset   { std::filesystem::path path; };
    struct RemovedAsset    { std::filesystem::path path; };
    struct RenamedAsset    { std::filesystem::path old_path; std::filesystem::path new_path; };
    struct AddedMeta       { std::filesystem::path path; };
    struct ModifiedMeta    { std::filesystem::path path; };
    struct RemovedMeta     { std::filesystem::path path; };
    struct RenamedMeta     { std::filesystem::path old_path; std::filesystem::path new_path; };
    struct AddedDirectory  { std::filesystem::path path; };
    struct RemovedDirectory{ std::filesystem::path path; };
    struct RenamedDirectory{ std::filesystem::path old_path; std::filesystem::path new_path; };
    struct RemovedUnknown  { std::filesystem::path path; bool is_meta; };
}

using AssetSourceEvent = std::variant<
    source_events::AddedAsset,
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
```

---

## Error Types

### `AssetReaderError`

```cpp
namespace reader_errors {
    struct NotFound { std::filesystem::path path; };
    struct IoError  { std::error_code code; };
    struct HttpError{ int status; };
}
using AssetReaderError = std::variant<
    reader_errors::NotFound,
    reader_errors::IoError,
    reader_errors::HttpError,
    std::exception_ptr>;
```

### `AssetWriterError`

```cpp
namespace writer_errors {
    struct IoError { std::error_code code; };
}
using AssetWriterError = std::variant<writer_errors::IoError, std::exception_ptr>;
```

---

## Implementing a custom reader

```cpp
struct MyReader : epix::assets::AssetReader {
    std::expected<std::unique_ptr<std::istream>, AssetReaderError>
    read(const std::filesystem::path& path) override
    {
        auto stream = open_my_archive(path);
        if (!stream)
            return std::unexpected(reader_errors::NotFound{path});
        return stream;
    }

    // … other overrides …
};
```

Register via `AssetSourceBuilder`:

```cpp
auto builder = AssetSourceBuilder::create(
    []() -> std::unique_ptr<AssetReader> { return std::make_unique<MyReader>(); });
plugin.register_asset_source("myarchive", std::move(builder));
```
