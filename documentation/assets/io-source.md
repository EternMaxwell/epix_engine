# I/O — Asset Sources

Register and build I/O back-ends: filesystem, memory (in-process), embedded (compiled-in binary data).

```cpp
#import epix.assets
```

---

## `AssetSourceBuilder`

Fluent builder for constructing a custom `AssetSource`.

```cpp
struct AssetSourceBuilder {
    using ReaderFactory  = std::function<std::unique_ptr<AssetReader>()>;
    using WriterFactory  = std::function<std::unique_ptr<AssetWriter>()>;
    using WatcherFactory = std::function<std::unique_ptr<AssetWatcher>(AssetServer&)>;

    // Create with a mandatory reader factory
    static AssetSourceBuilder create(ReaderFactory reader);

    // Optional components
    AssetSourceBuilder& with_writer(WriterFactory);
    AssetSourceBuilder& with_processed_reader(ReaderFactory);
    AssetSourceBuilder& with_processed_writer(WriterFactory);
    AssetSourceBuilder& with_watcher(WatcherFactory);

    // Build using the OS filesystem (reads from a directory path)
    static AssetSourceBuilder platform_default(std::filesystem::path root);

    AssetSource build(std::string_view source_id);
};
```

### Register a custom source

```cpp
auto dir = memory::Directory::create({});

auto builder = AssetSourceBuilder::create(
    [dir]() -> std::unique_ptr<AssetReader> {
        return std::make_unique<MemoryAssetReader>(dir);
    })
    .with_writer(
        [dir]() -> std::unique_ptr<AssetWriter> {
            return std::make_unique<MemoryAssetWriter>(dir);
        });

plugin.register_asset_source("live-data", std::move(builder));

// Then load from it: server.load<Image>("live-data://sprite.png")
```

---

## `AssetSource`

A built source. Holds optional reader, writer, processed-reader, processed-writer, and watcher.

```cpp
struct AssetSource {
    AssetReader*  reader() const;               // may be nullptr
    AssetWriter*  writer() const;               // may be nullptr
    AssetReader*  processed_reader() const;
    AssetWriter*  processed_writer() const;
    AssetWatcher* watcher() const;

    std::string_view id() const;
};
```

`AssetSources` (resource injected by `AssetPlugin`) holds all built sources indexed by their id.

---

## `AssetSources`

```cpp
struct AssetSources {
    const AssetSource* get(std::string_view id) const;     // nullptr if not found
    const AssetSource& default_source() const;
};
```

---

## `EmbeddedAssetRegistry`

In-process source for assets compiled into the binary via `xxd` / `cmake --embed` / similar tools.

```cpp
// Sentinel source id for the embedded source
constexpr std::string_view EMBEDDED = "embedded";

struct EmbeddedAssetRegistry {
    // Insert a virtual file backed by a heap-allocated buffer
    void insert_asset(std::string_view path, std::vector<uint8_t> data);

    // Insert a virtual file backed by a static byte array (zero-copy)
    void insert_asset_static(std::string_view path,
                              std::span<const uint8_t> data);

    // Insert a sidecar .meta file
    void insert_meta(std::string_view path, std::vector<uint8_t> data);

    // Remove a virtual file
    void remove_asset(std::string_view path);

    // Register this registry as an AssetSource with the given AssetPlugin
    void register_source(AssetPlugin& plugin);
};
```

```cpp
EmbeddedAssetRegistry registry;
registry.insert_asset_static("shaders/pbr.slang",
    std::span<const uint8_t>(pbr_slang_bytes, pbr_slang_bytes_len));
registry.insert_asset_static("fonts/default.ttf",
    std::span<const uint8_t>(default_ttf, default_ttf_len));
registry.register_source(plugin);

// Later:
Handle<Shader> sh = server.load<Shader>("embedded://shaders/pbr.slang");
```

---

## `MemoryAssetReader`

Concrete `AssetReader` backed by a `memory::Directory` (shared in-process tree).

```cpp
struct MemoryAssetReader : AssetReader {
    explicit MemoryAssetReader(std::shared_ptr<memory::Directory> root);

    // inherits all AssetReader overrides
};
```

---

## `MemoryAssetWriter`

Concrete `AssetWriter` backed by a `memory::Directory`.

```cpp
struct MemoryAssetWriter : AssetWriter {
    explicit MemoryAssetWriter(std::shared_ptr<memory::Directory> root);

    // inherits all AssetWriter overrides
};
```

---

## `MemoryAssetWatcher`

Concrete `AssetWatcher` that receives events when `memory::Directory` entries change.

```cpp
struct MemoryAssetWatcher : AssetWatcher {
    explicit MemoryAssetWatcher(std::shared_ptr<memory::Directory> root);

    std::vector<AssetSourceEvent> drain_events() override;
};
```

### Building a fully live in-process source

```cpp
auto dir    = memory::Directory::create({});
auto reader = [dir]{ return std::make_unique<MemoryAssetReader>(dir); };
auto writer = [dir]{ return std::make_unique<MemoryAssetWriter>(dir); };
auto watch  = [dir](AssetServer&) {
                  return std::make_unique<MemoryAssetWatcher>(dir);
              };

AssetSourceBuilder::create(reader)
    .with_writer(writer)
    .with_watcher(watch);
```
