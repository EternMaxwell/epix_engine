# AssetPlugin

Configure and register the asset system into an `App`.

```cpp
#import epix.assets
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

    void build(App&);
    void finish(App&);

    // Register a custom asset source before build() is called
    AssetPlugin& register_asset_source(AssetSourceId id, AssetSourceBuilder);
};
```

`AssetPlugin::build()` inserts:
- `AssetServer` resource (backed by IOTaskPool)
- `AssetSources` resource (built from registered sources)
- Default OS filesystem source reading from `file_path`

`AssetPlugin::finish()` starts background load workers and activates watch streams.

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
void app_register_asset(App& app);
```

- Inserts `Assets<T>` resource.
- Adds `Assets<T>::handle_events` and `Assets<T>::asset_events` systems in the correct system sets.
- Must be called after `AssetPlugin` has been added.

```cpp
app.add_plugin(AssetPlugin{});
app_register_asset<Image>(app);
app_register_asset<Shader>(app);
```

---

## `app_register_loader<T>()`

```cpp
template<AssetLoader T>
void app_register_loader(App& app, T loader = {});
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
void app_preregister_loader(App& app, std::span<std::string_view> extensions);
```

Maps `extensions` to loader type `T` before any instance is constructed. Useful in plugins that
want to advertise supported formats to the rest of the app without installing the full loader yet.

---

## `app_register_asset_processor<P>()`

```cpp
template<Process P>
void app_register_asset_processor(App& app, P processor);
```

Registers a processor with the `AssetProcessor` resource. Requires `AssetServerMode::Processed`.

---

## `app_set_default_asset_processor<P>()`

```cpp
template<Process P>
void app_set_default_asset_processor(App& app, std::string_view extension);
```

Sets `P` as the default processor for `extension` (e.g., `"png"`, `"glsl"`). When the processor
encounters a file with no explicit `.meta` override, it will run `P`.
