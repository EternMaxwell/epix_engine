# EPIX ENGINE ASSETS MODULE

Asynchronous asset management: loading, caching, hot-reloading, and processing of typed assets from the filesystem, memory, or embedded binary data.

## Core Parts

- **[`Asset`](./plugin.md)** — concept: any movable type can be an asset.
- **[`AssetPlugin`](./plugin.md)** — app plugin: configures sources, mode, and builds all asset systems.
- **[`app_register_asset<T>()`](./plugin.md)** — registers an asset type on a running app.
- **[`app_register_loader<T>()`](./plugin.md)** — registers an asset loader on a running app.
- **[`AssetSystems`](./plugin.md)** — built-in system sets (HandleEvents, WriteEvents).
- **[`AssetServer`](./asset-server.md)** — central server: loads assets asynchronously, tracks load state.
- **[`Assets<T>`](./asset-store.md)** — typed collection: stores loaded assets, emits lifecycle events.
- **[`AssetEvent<T>`](./asset-store.md)** — lifecycle event: Added, Removed, Modified, Unused, LoadedWithDependencies.
- **[`Handle<T>`](./asset-handle.md)** — strong or weak typed handle to an asset; keeps the asset alive when strong.
- **[`UntypedHandle`](./asset-handle.md)** — type-erased handle, convertible to/from `Handle<T>`.
- **[`AssetId<T>`](./asset-handle.md)** — typed identifier (index or UUID).
- **[`UntypedAssetId`](./asset-handle.md)** — type-erased identifier.
- **[`AssetPath`](./asset-path.md)** — full asset address: `[source://]path[#label]`.
- **[`AssetSourceId`](./asset-path.md)** — optional source name; default source has no name.
- **[`AssetLoader`](./asset-loader.md)** — concept: loaders deserialise a byte stream into a typed asset.
- **[`LoadContext`](./asset-loader.md)** — context passed to loaders for sub-asset registration.
- **[`NestedLoader`](./asset-loader.md)** — builder for loading sub-assets from inside a loader.
- **[`LoadedAsset<A>`](./asset-loader.md)** — typed wrapper holding the loaded value plus dependency tracking.
- **[`AssetSaver`](./asset-saver-transformer.md)** — concept: savers write a typed asset back to a stream.
- **[`AssetTransformer`](./asset-saver-transformer.md)** — concept: transformers convert one asset type to another.
- **[`SavedAsset<A>`](./asset-saver-transformer.md)** — read-only wrapper used inside saver implementations.
- **[`TransformedAsset<A>`](./asset-saver-transformer.md)** — mutable wrapper used inside transformer implementations.
- **[`Process`](./asset-processor.md)** — concept: processors run inside the build pipeline, transforming raw → processed bytes.
- **[`AssetProcessor`](./asset-processor.md)** — runtime processor: coordinates the asset build pipeline.
- **[`ProcessContext`](./asset-processor.md)** — context passed to processors; provides input stream and sub-asset server access.
- **[`AssetReader`](./io-reader-writer.md)** / **[`AssetWriter`](./io-reader-writer.md)** / **[`AssetWatcher`](./io-reader-writer.md)** — abstract I/O interfaces.
- **[`AssetSource`](./io-source.md)** — built-in source pairing a reader/writer/watcher.
- **[`AssetSourceBuilder`](./io-source.md)** — fluent builder for custom asset sources.
- **[`EmbeddedAssetRegistry`](./io-source.md)** — in-binary asset source backed by a `memory::Directory`.
- **[`MemoryAssetReader`](./io-source.md)** / **[`MemoryAssetWriter`](./io-source.md)** / **[`MemoryAssetWatcher`](./io-source.md)** — in-memory I/O implementations.
- **[`Settings`](./asset-meta.md)** — base class every loader/processor settings type must derive from.
- **[`AssetMetaCheck`](./asset-meta.md)** — controls when `.meta` sidecar files are checked.
- **[`AssetHash`](./asset-meta.md)** — 32-byte BLAKE3 hash used by the processor pipeline.

## Quick Guide

```cpp
import epix.assets;
import epix.core;
using namespace epix::assets;
using namespace epix::core;

// 1. Define your asset type (any movable type satisfies Asset)
struct Image {
    int width, height;
    std::vector<uint8_t> pixels;
};

// 2. Define a loader
struct PngLoader {
    using Asset    = Image;
    struct Settings : epix::assets::Settings {};
    using Error    = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static std::array exts = { std::string_view{"png"} };
        return exts;
    }
    static std::expected<Image, Error> load(
        std::istream& stream,
        const Settings&,
        LoadContext&)
    {
        // decode PNG bytes from stream into Image …
        return Image{ 8, 8, {} };
    }
};

// 3. Build the app
int main() {
    App app = App::create();
    app.add_plugin(AssetPlugin{});                     // adds AssetServer resource
    app_register_asset<Image>(app);                    // adds Assets<Image> resource + events
    app_register_loader<PngLoader>(app);               // records loader mapping .png → Image

    // In a startup system: load from the default "assets/" folder
    app.add_systems(Startup, [](Res<AssetServer> server) {
        Handle<Image> icon = server->load<Image>("icon.png");
        // Handle is strong — asset stays alive as long as you hold it
    });

    // React to load completion
    app.add_systems(Update, [](EventReader<AssetEvent<Image>> events, Res<Assets<Image>> store) {
        for (auto& e : events.read()) {
            if (e.is_loaded_with_dependencies()) {
                auto& image = store->get(e.id)->get();
                std::println("Image fully loaded: id={}, width={}, height={}",
                    e.id.to_string_short(), image.width, image.height);
            }
        }
    });

    app.run();
}
```
