# Asset Sources

```cpp
import epix.assets;
```

An `AssetSource` groups a required asynchronous `AssetReader` with optional
writers, processed readers/writers, watchers, and watcher event channels.

## AssetSourceBuilder

```cpp
auto builder = AssetSourceBuilder::create(reader_factory)
    .with_writer(writer_factory)
    .with_processed_reader(processed_reader_factory)
    .with_ungated_processed_reader(ungated_reader_factory)
    .with_processed_writer(processed_writer_factory)
    .with_watcher(watcher_factory)
    .with_processed_watcher(processed_watcher_factory);

AssetSource source = builder.build(source_id, watch, watch_processed);
```

Reader and writer factories take no arguments. Watcher factories receive an
`async_channel::Sender<AssetSourceEvent>` and return an `AssetWatcher`.
`platform_default(root, processed_root)` builds filesystem-backed factories.

```cpp
auto directory = memory::Directory::create({});

auto source = AssetSourceBuilder::create([directory] {
        return std::make_unique<MemoryAssetReader>(directory);
    })
    .with_writer([directory] {
        return std::make_unique<MemoryAssetWriter>(directory);
    })
    .with_watcher([directory](async_channel::Sender<AssetSourceEvent> sender) {
        return std::make_unique<MemoryAssetWatcher>(
            directory,
            [sender = std::move(sender)](AssetSourceEvent event) mutable {
                sender.try_send(std::move(event));
            });
    });
```

## Source Collections

`AssetSourceBuilders` stores the default and named builders, and
`build_sources(watch, watch_processed)` produces `AssetSources`. `AssetSources`
provides lookup, iteration, processed-source iteration, IDs, and processor
gating.

`AssetSourceId` identifies the default source or a named source. An `AssetPath`
such as `textures/player.png` uses the default source; a path such as
`memory://textures/player.png` uses the named source.

## Built-in Implementations

- Filesystem: `FileAssetReader`, `FileAssetWriter`, and `FileAssetWatcher`.
- Memory: `MemoryAssetReader`, `MemoryAssetWriter`, and `MemoryAssetWatcher`
  over `memory::Directory`.
- Embedded: `EmbeddedAssetRegistry` and its reader for compiled-in data.
- Processor gating: `ProcessorGatedReader` prevents runtime reads from racing
  asset processing.
