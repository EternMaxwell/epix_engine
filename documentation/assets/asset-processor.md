# Asset Processor

Offline-style pipeline that reads raw source assets, transforms them, and writes processed output.  
Requires `AssetServerMode::Processed`.

```cpp
#import epix.assets
```

---

## `Process` concept

```cpp
template<typename T>
concept Process = requires {
    typename T::Settings;      // must satisfy is_settings (plain aggregate, zpp::bits-serializable)
    typename T::OutputLoader;  // loader that can reload the processed output

    { T::process(std::declval<ProcessContext&>(),
                 std::declval<const typename T::Settings&>(),
                 std::declval<std::ostream&>()) }
        -> std::convertible_to<
               std::expected<typename T::OutputLoader::Settings, std::exception_ptr>>;
};
```

A processor reads from `ProcessContext` (provides the raw input stream), writes processed bytes to the `ostream`, and returns the settings the `OutputLoader` should use when reading the output back.

### Minimal example

```cpp
struct OptimisePng {
    struct Settings { int level = 6; };  // plain aggregate — no inheritance needed
    using OutputLoader = PngLoader;

    static std::expected<PngLoader::Settings, std::exception_ptr> process(
        ProcessContext& ctx,
        const Settings& settings,
        std::ostream& output)
    try {
        // ctx.reader() returns the raw input istream
        auto raw = std::ostringstream{};
        raw << ctx.reader().rdbuf();
        auto optimised = png_optimise(raw.str(), settings.level);
        output.write(optimised.data(), optimised.size());
        return PngLoader::Settings{};
    } catch (...) {
        return std::unexpected(std::current_exception());
    }
};
```

Register with:

```cpp
app_register_asset_processor<OptimisePng>(app, OptimisePng{});
app_set_default_asset_processor<OptimisePng>(app, "png");
```

---

## `ProcessContext`

Context provided to `Process::process()`.

```cpp
struct ProcessContext {
    // The raw input stream for the source asset
    std::istream& reader();

    // The path of the asset being processed
    const AssetPath& path() const;

    // Access the source AssetServer (read-only; for loading dependencies)
    const AssetServer& asset_server() const;

    // Record a dependency that should trigger re-processing when changed
    void add_dependency(AssetPath path, AssetHash hash);
};
```

---

## `AssetProcessor`

Coordinates the full processing pipeline. Available as a resource when `AssetServerMode::Processed` is active.

```cpp
struct AssetProcessor {
    ProcessorState state() const;

    // Block until all pending processing is finished
    void wait_until_finished() const;

    // Manually trigger re-processing for a specific path
    void process_path(AssetPath path);
};
```

### `ProcessorState`

```cpp
enum class ProcessorState { Initializing, Processing, Finished };
```

| State          | Meaning                                   |
| -------------- | ----------------------------------------- |
| `Initializing` | Scanning source and processed directories |
| `Processing`   | Actively running processor tasks          |
| `Finished`     | All known assets are up to date           |

---

## `ErasedProcessor`

Abstract base interface used internally for storing heterogeneous processors.

```cpp
struct ErasedProcessor {
    virtual ProcessResult process(
        ProcessContext&, std::ostream&) = 0;

    virtual std::unique_ptr<Settings>  deserialize_meta(std::istream&) = 0;
    virtual std::string                type_path() const = 0;
    virtual std::unique_ptr<Settings>  default_meta() const = 0;
};
```

`ErasedProcessor` instances are created automatically by `app_register_asset_processor<P>()`.

---

## Process Results and Errors

### `ProcessResult`

```cpp
struct ProcessResult {
    ProcessResultKind kind;
    ProcessedInfo     processed_info;

    static ProcessResult processed(ProcessedInfo);
    static ProcessResult skipped_not_changed();
    static ProcessResult ignored();
};

enum class ProcessResultKind { Processed, SkippedNotChanged, Ignored };
```

### `ProcessStatus`

```cpp
enum class ProcessStatus { Processed, Failed, NonExistent };
```

### `ProcessError`

A variant of all possible pipeline-level errors.

```cpp
namespace process_errors {
    struct MissingAssetLoader     {};
    struct NoSupportedLoaderFound {};
    struct AssetLoadError         { epix::assets::AssetLoadError error; };
    struct AssetSaveError         { std::exception_ptr error; };
    struct MissingAssetWriter     {};
    struct MissingAssetReader     { AssetReaderError error; };
    struct MissingProcessedAssetReader { AssetReaderError error; };
    struct MissingProcessedWriter {};
    struct ProcessingError        { std::exception_ptr error; };
    struct WriteError             { std::exception_ptr error; };
    struct ExtensionRequired      {};
    struct FullPathNotAllowed     {};
}

using ProcessError = std::variant<
    process_errors::MissingAssetLoader,
    process_errors::NoSupportedLoaderFound,
    process_errors::AssetLoadError,
    process_errors::AssetSaveError,
    process_errors::MissingAssetWriter,
    process_errors::MissingAssetReader,
    process_errors::MissingProcessedAssetReader,
    process_errors::MissingProcessedWriter,
    process_errors::ProcessingError,
    process_errors::WriteError,
    process_errors::ExtensionRequired,
    process_errors::FullPathNotAllowed>;
```

---

## `ProcessorTransactionLog`

Abstract interface for the write-ahead log that protects against incomplete processing runs.

```cpp
struct ProcessorTransactionLog {
    virtual void begin_processing(AssetPath path)  = 0;
    virtual void end_processing(AssetPath path)    = 0;
    virtual void unrecoverable()                   = 0;
};

struct ProcessorTransactionLogFactory {
    virtual ProcessorTransactionLog* read()           = 0;   // open existing log
    virtual ProcessorTransactionLog* create_new_log() = 0;   // create empty log
};
```

`validate_transaction_log(factory)` reads the log file, identifies any incomplete transactions
from previous runs, and returns a `ValidateLogError` if recovery is impossible.

```cpp
std::expected<void, ValidateLogError> validate_transaction_log(
    ProcessorTransactionLogFactory& factory);
```
