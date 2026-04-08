# Asset Meta

Controls how `.meta` sidecar files and loader/processor settings interact with the pipeline.

```cpp
#import epix.assets
```

---

## `Settings`

Base class that every loader, saver, transformer, and processor settings struct must derive from.

```cpp
struct Settings {};
```

```cpp
struct PngLoaderSettings : epix::assets::Settings {
    bool srgb    = true;
    bool mipmaps = false;
};
```

The settings struct is passed by the framework to every `load()`, `save()`, `transform()`, and `process()` call. Default-constructed settings are used unless overridden at the call site or (once implemented) by a `.meta` sidecar file.

---

## `AssetMetaCheck`

Controls when and whether the `AssetServer` looks for `.meta` sidecar files.

```cpp
namespace asset_meta_check {
    struct Always {};
    struct Never  {};
    struct Paths  { std::unordered_set<std::filesystem::path> paths; };
}
using AssetMetaCheck = std::variant<
    asset_meta_check::Always,
    asset_meta_check::Never,
    asset_meta_check::Paths>;
```

| Variant           | Behaviour                                       |
| ----------------- | ----------------------------------------------- |
| `Always`          | Check for `.meta` alongside every asset load    |
| `Never` (default) | Never check; always use default loader settings |
| `Paths{…}`        | Check only the listed paths                     |

Set on `AssetPlugin`:

```cpp
AssetPlugin plugin;
plugin.meta_check = asset_meta_check::Always{};
```

> **Note:** `.meta` file *contents* cannot be serialized or deserialized yet — see [todo.md](./todo.md#meta-serialization).

---

## `UnapprovedPathMode`

Determines how the server handles asset paths that are not explicitly allow-listed.

```cpp
enum class UnapprovedPathMode { Allow, Deny, Forbid };
```

| Value             | Behaviour                                      |
| ----------------- | ---------------------------------------------- |
| `Allow` (default) | Any path may be loaded                         |
| `Deny`            | Unapproved paths emit a warning but still load |
| `Forbid`          | Unapproved paths fail with an error            |

---

## `AssetHash`

32-byte BLAKE3 content hash used by the processor pipeline to detect unchanged files.

```cpp
using AssetHash = std::array<uint8_t, 32>;
```

---

## `ProcessedInfo`

Stored in the `.meta` file next to each processed asset. Records hash and dependencies.

```cpp
struct ProcessDependencyInfo {
    AssetHash full_hash;
    AssetPath path;
};

struct ProcessedInfo {
    AssetHash                          hash;               // hash of the processed output
    AssetHash                          full_hash;          // hash of source + all dependencies
    std::vector<ProcessDependencyInfo> process_dependencies;
};
```

---

## `AssetActionType` / `AssetAction`

Describes what the pipeline should do with a given asset. Stored inside `.meta` files.

```cpp
enum class AssetActionType { Load, Process, Ignore };

template<typename LoaderSettings, typename ProcessorSettings>
struct AssetAction {
    using Load    = LoaderSettings;
    using Process = ProcessorSettings;
    using Ignore  = std::monostate;

    std::variant<Load, Process, Ignore> action;

    AssetActionType type() const;
};
```

| Type      | Meaning                                                                   |
| --------- | ------------------------------------------------------------------------- |
| `Load`    | Use the `LoaderSettings` stored in the `.meta` file when loading          |
| `Process` | Run the processor with the `ProcessorSettings` stored in the `.meta` file |
| `Ignore`  | Skip this asset entirely                                                  |
