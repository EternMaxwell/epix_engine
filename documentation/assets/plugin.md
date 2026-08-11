# AssetPlugin

Configure and register the asset system into an `App`.

```cpp
import epix.assets;
```

---

## `AssetPlugin`

```cpp
struct AssetPlugin {
    std::filesystem::path                file_path;                // default: "assets"
    std::optional<std::filesystem::path> processed_file_path;      // default: "processed_assets"
    std::optional<std::filesystem::path> embedded_processed_path;  // default: std::nullopt
    AssetServerMode    mode;                   // Processed (default)
    std::optional<bool> watch_for_changes_override;      // default: std::nullopt
    std::optional<bool> use_asset_processor_override;     // default: std::nullopt
    AssetMetaCheck     meta_check;             // Always (default)
    UnapprovedPathMode unapproved_path_mode;   // Forbid (default)

    void attach(app::App&);
    void ready(app::App&);

    // Register a custom asset source before attach() is called
    AssetPlugin& register_asset_source(AssetSourceId id, AssetSourceBuilder);
};
```

`AssetPlugin::attach()` performs the complete setup:

- creates or reuses `AssetSourceBuilders`, initializes the default filesystem source, and
  registers the embedded in-memory source;
- builds an `AssetServer` from those sources;
- in processed mode, creates `AssetProcessor` when processing is enabled and a processed output
  path exists, then starts it in `Startup`;
- inserts `EmbeddedAssetRegistry`;
- registers the built-in `LoadedFolder` and `LoadedUntypedAsset` asset types plus typed and
  untyped failure events; and
- orders `AssetSystems::HandleEvents` before `AssetSystems::WriteEvents`.

`AssetPlugin::ready()` is currently a no-op. Loading tasks are spawned by `AssetServer` as loads
are requested; filesystem watchers are constructed as part of source setup when watching is
enabled.

### Fields

| Field                          | Type                 | Default              | Description                                                                                                                                       |
| ------------------------------ | -------------------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `file_path`                    | `path`               | `"assets"`           | Filesystem root for the default asset source                                                                                                      |
| `processed_file_path`          | `optional<path>`     | `"processed_assets"` | Directory for processed asset output                                                                                                              |
| `embedded_processed_path`      | `optional<path>`     | `nullopt`            | Processed-asset directory for the embedded source. When `nullopt`, embedded assets stay on their in-memory reader and skip the processor pipeline |
| `mode`                         | `AssetServerMode`    | `Processed`          | Server operating mode                                                                                                                             |
| `watch_for_changes_override`   | `optional<bool>`     | `nullopt`            | Override for file-watching behaviour                                                                                                              |
| `use_asset_processor_override` | `optional<bool>`     | `nullopt`            | Override for processor usage in Processed mode                                                                                                    |
| `meta_check`                   | `AssetMetaCheck`     | `Always`             | When to look for `.meta` sidecar files                                                                                                            |
| `unapproved_path_mode`         | `UnapprovedPathMode` | `Forbid`             | How unapproved asset paths are handled                                                                                                            |

### `AssetServerMode`

```cpp
enum class AssetServerMode { Unprocessed, Processed };
```

In processed mode, `use_asset_processor_override.value_or(true)` enables the processor. It is
created only when `processed_file_path` has a value. The server forces metadata checking to
`Always` in processed mode. File watching defaults to `false`; set
`watch_for_changes_override = true` to opt in.

| Mode          | Description                                                                  |
| ------------- | ---------------------------------------------------------------------------- |
| `Unprocessed` | Loads raw files from `file_path`                                             |
| `Processed`   | Loads from `processed_file_path` (asset build pipeline output) — **default** |

---

## `AssetSystems`

```cpp
enum class AssetSystems { HandleEvents, WriteEvents };
```

System set labels for ordering custom systems relative to asset infrastructure.

| Set            | When it runs                                                                    |
| -------------- | ------------------------------------------------------------------------------- |
| `HandleEvents` | Consumes internal load/error events and updates `Assets<T>` + handle ref-counts |
| `WriteEvents`  | Emits public `AssetEvent<T>` and `AssetLoadFailedEvent<T>` to `EventWriter`     |

---

## `app_register_asset<T>()`

```cpp
template<Asset T>
app::App& app_register_asset(app::App& app);
```

- Inserts `Assets<T>` resource.
- Adds `Assets<T>::handle_events` and `Assets<T>::asset_events` systems in the correct system sets.
- Must be called after `AssetPlugin` has been added.

```cpp
app.add_plugins(AssetPlugin{});
app_register_asset<Image>(app);
app_register_asset<Shader>(app);
```

---

## `app_register_loader<T>()`

```cpp
template<AssetLoader T>
app::App& app_register_loader(app::App& app, const T& loader = T{});
```

Registers `T` with the `AssetServer`, mapping the extensions returned by `T::extensions()` to
`T::Asset`. The single `loader` instance can carry initialisation data.

```cpp
app_register_loader<PngLoader>(app);
app_register_loader<JsonLoader>(app, JsonLoader{.strict = true});
```

---

## `app_preregister_loader<T>()`

```cpp
template<AssetLoader T>
app::App& app_preregister_loader(app::App& app, std::span<std::string_view> extensions);
```

Maps `extensions` to loader type `T` before any instance is constructed. Useful in plugins that
want to advertise supported formats to the rest of the app without installing the full loader yet.

---

## `app_register_asset_processor<P>()`

```cpp
template<Process P>
app::App& app_register_asset_processor(app::App& app, P processor);
```

Registers a processor with the `AssetProcessor` resource. It throws when that resource is absent,
so add `AssetPlugin` in processed mode with processor use and a processed path first.

---

## `app_set_default_asset_processor<P>()`

```cpp
template<Process P>
app::App& app_set_default_asset_processor(app::App& app, const std::string& extension);
```

Sets `P` as the default processor for `extension` (e.g., `"png"`, `"glsl"`). When the processor
encounters a file with no explicit `.meta` override, it will run `P`.

Like `app_register_asset_processor`, this helper throws if `AssetProcessor` is absent.
