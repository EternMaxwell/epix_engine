# Asset Processing

```cpp
import epix.assets;
```

The processing pipeline reads source assets, applies a registered `Process`,
writes processed bytes and metadata, tracks dependencies, and skips unchanged
inputs using timestamps or hashes.

## Process Concept

A process declares default-constructible serializable `Settings` and an
`OutputLoader`, then writes its result asynchronously:

```cpp
struct MyProcess {
    struct Settings {};
    using OutputLoader = MyProcessedLoader;

    STDEXEC::task<std::expected<OutputLoader::Settings, std::exception_ptr>>
    process(ProcessContext& context,
            const Settings& settings,
            Writer& output);
};
```

`ProcessContext` provides the source `Reader`, current path, dependency loading,
and access to registered loaders/savers/transformers. Processing code should
`co_await` reader/writer and nested-load operations rather than blocking a task
pool thread.

Register processors with `app_register_asset_processor(app, processor)` and set
an extension default with `app_set_default_asset_processor`.

## Runtime

`AssetProcessor` owns source access, processor registries, dependency state,
transaction logs, and asynchronous status channels. It reports
`ProcessResultKind::Processed`, `SkippedNotChanged`, or `Ignored`, and tracks a
final `ProcessStatus` of `Processed`, `Failed`, or `NonExistent`.

`AssetPlugin` chooses processed vs. unprocessed mode and wires file/memory source
watchers into the processor. Processed readers are gated during initialization
so runtime loads do not observe partially written output.
