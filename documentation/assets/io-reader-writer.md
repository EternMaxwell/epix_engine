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
        read(const std::filesystem::path& path) = 0;

    // Open a stream for the sidecar .meta file
    virtual std::expected<std::unique_ptr<std::istream>, AssetReaderError>
        read_meta(const std::filesystem::path& path) = 0;

    // Read the sidecar .meta file into a byte buffer
    virtual std::expected<std::vector<uint8_t>, AssetReaderError>
        read_meta_bytes(const std::filesystem::path& path) = 0;

    // List all entries in a directory (non-recursive)
    virtual std::expected<std::vector<std::filesystem::path>, AssetReaderError>
        read_directory(const std::filesystem::path& path) = 0;

    // Test whether path refers to a directory
    virtual std::expected<bool, AssetReaderError>
        is_directory(const std::filesystem::path& path) = 0;
};
```

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

Abstract base for receiving filesystem change notifications.

```cpp
struct AssetWatcher {
    // Called by the runtime to drain pending events
    virtual std::vector<AssetSourceEvent> drain_events() = 0;
};
```

---

## `AssetSourceEvent`

A change event from the watcher. Variant of all event kinds.

```cpp
namespace source_events {
    struct AddedAsset      { std::filesystem::path path; };
    struct ModifiedAsset   { std::filesystem::path path; };
    struct RemovedAsset    { std::filesystem::path path; };
    struct RenamedAsset    { std::filesystem::path from; std::filesystem::path to; };
    struct AddedMeta       { std::filesystem::path path; };
    struct ModifiedMeta    { std::filesystem::path path; };
    struct RemovedMeta     { std::filesystem::path path; };
    struct RenamedMeta     { std::filesystem::path from; std::filesystem::path to; };
    // … additional internal variants
}

using AssetSourceEvent = std::variant<
    source_events::AddedAsset,
    source_events::ModifiedAsset,
    source_events::RemovedAsset,
    source_events::RenamedAsset,
    source_events::AddedMeta,
    source_events::ModifiedMeta,
    source_events::RemovedMeta,
    source_events::RenamedMeta
    /* … */>;
```

---

## Error Types

### `AssetReaderError`

```cpp
namespace reader_errors {
    struct NotFound { std::filesystem::path path; };
    struct IoError  { std::string message; };
    struct HttpError{ int status_code; };
}
using AssetReaderError = std::variant<
    reader_errors::NotFound,
    reader_errors::IoError,
    reader_errors::HttpError>;
```

### `AssetWriterError`

```cpp
namespace writer_errors {
    struct IoError { std::string message; };
}
using AssetWriterError = std::variant<writer_errors::IoError>;
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
