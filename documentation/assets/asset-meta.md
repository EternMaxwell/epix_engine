# Asset metadata

Metadata controls loader settings, processing, and path policy. Sidecar files use format version `META_FORMAT_VERSION == "2.0"`.

```cpp
import epix.assets;
using namespace epix::assets;
```

## Settings

Concrete settings are default-constructible, zpp::bits-serializable aggregates. They do not inherit from `Settings`; the engine wraps them in `SettingsImpl<T>` for type-erased storage.

```cpp
struct ImageSettings {
    bool srgb = true;
    bool mipmaps = false;
};
```

`Settings::try_cast<T>()` returns an optional reference wrapper and `cast<T>()` returns a reference. `EmptySettings` is used when a stage has no options.

## Metadata lookup

```cpp
namespace asset_meta_check {
    struct Always {};
    struct Never {};
    struct Paths { std::unordered_set<AssetPath> paths; };
}
using AssetMetaCheck = std::variant<asset_meta_check::Always,
                                    asset_meta_check::Never,
                                    asset_meta_check::Paths>;
```

`Always` is the default, `Never` skips sidecars, and `Paths` limits checks to the listed asset paths. Configure the policy through `AssetPlugin::meta_check`.

## Unapproved paths

```cpp
enum class UnapprovedPathMode { Allow, Deny, Forbid };
```

- `Allow` permits any path.
- `Deny` rejects an unapproved path unless an override loading method is used.
- `Forbid` always rejects it and is the default.

`AssetPath::is_unapproved()` identifies absolute, rooted, prefixed, or parent-escaping paths.

## Processing data

`AssetHash` is a 32-byte BLAKE3 hash. `ProcessedInfo` stores the asset hash, full dependency hash, optional source modification timestamp, and `ProcessDependencyInfo` records. These values are serialized into processed metadata.

An action contains its settings explicitly:

```cpp
template<class LoaderSettings, class ProcessSettings>
struct AssetAction {
    struct Load { LoaderSettings settings{}; };
    struct Process {
        ProcessSettings settings{};
        std::string processor;
    };
    struct Ignore {};

    std::variant<Load, Process, Ignore> inner = Load{};
    AssetActionType type() const;
};
```

`AssetActionType` is `Load`, `Process`, or `Ignore`.
