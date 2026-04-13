# Asset Meta

Controls how `.meta` sidecar files and loader/processor settings interact with the pipeline.

```cpp
#import epix.assets
```

---

## `Settings` / `SettingsImpl<T>` / `is_settings`

Polymorphic base class for loader/processor settings. Concrete settings types are **plain
aggregates** — they do not inherit from `Settings`. The engine wraps them in `SettingsImpl<T>`
for polymorphic storage.

```cpp
struct Settings {
    virtual ~Settings() = default;
    template<typename T> std::optional<std::reference_wrapper<const T>> try_cast() const;
    template<typename T> const T& cast() const;
};

struct EmptySettings {};  // default when no settings are needed

template<typename T>
concept is_settings = std::is_default_constructible_v<T> && /* zpp::bits-serializable */;

template<typename T>
struct SettingsImpl : Settings { T value{}; };
```

Settings must satisfy the `is_settings` concept, which requires the type to be
default-constructible and serializable by zpp::bits (aggregates, containers,
`std::optional`, `std::variant`, empty types, or types with an explicit serialize hook).

```cpp
// Define settings as a plain aggregate — no inheritance required
struct PngLoaderSettings {
    bool srgb    = true;
    bool mipmaps = false;
};
// The engine stores it as SettingsImpl<PngLoaderSettings> internally
```

The settings struct is passed by the framework to every `load()`, `save()`, `transform()`, and `process()` call. Default-constructed settings are used unless overridden at the call site or by a `.meta` sidecar file.

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

| Variant            | Behaviour                                       |
| ------------------ | ----------------------------------------------- |
| `Always` (default) | Check for `.meta` alongside every asset load    |
| `Never`            | Never check; always use default loader settings |
| `Paths{…}`         | Check only the listed paths                     |

Set on `AssetPlugin`:

```cpp
AssetPlugin plugin;
plugin.meta_check = asset_meta_check::Never{}; // override default Always
```

---

## `UnapprovedPathMode`

Determines how the server handles asset paths that are not explicitly allow-listed.

```cpp
enum class UnapprovedPathMode { Allow, Deny, Forbid };
```

| Value              | Behaviour                                      |
| ------------------ | ---------------------------------------------- |
| `Allow`            | Any path may be loaded                         |
| `Deny`             | Unapproved paths emit a warning but still load |
| `Forbid` (default) | Unapproved paths fail with an error            |

---

## `AssetHash`

32-byte BLAKE3 content hash used by the processor pipeline to detect unchanged files.

```cpp
using AssetHash = std::array<uint8_t, 32>;
```

---

## `META_FORMAT_VERSION`

Version string for the binary `.meta` format. Bumped when the serialization layout changes.

```cpp
inline constexpr std::string_view META_FORMAT_VERSION = "2.0";
```

---

## `ProcessedInfo`

Stored in the `.meta` file next to each processed asset. Records hashes, dependencies, and an
optional source-file timestamp for fast-path skip logic.

```cpp
struct ProcessDependencyInfo {
    AssetHash   full_hash;   // hash of the dependency
    std::string path;        // path of the dependency asset
};

struct ProcessedInfo {
    AssetHash                          hash;               // hash of the processed output
    AssetHash                          full_hash;          // hash of source + all dependencies
    std::optional<std::int64_t>        source_mtime_ns;    // source last-modified (nanoseconds)
    std::vector<ProcessDependencyInfo> process_dependencies;
};
```

`source_mtime_ns` is a cross-session optimisation: when the source file's last-modified time
(nanosecond precision) matches the stored value, the processor skips the expensive BLAKE3 hash
comparison entirely. Readers that do not support timestamps (e.g. `EmbeddedAssetReader`) leave
this field as `nullopt`, in which case the hash path is always taken.

Both `ProcessedInfo` and `ProcessDependencyInfo` are serialized with **zpp::bits** and stored
inside `.meta` sidecar files next to processed assets.

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
