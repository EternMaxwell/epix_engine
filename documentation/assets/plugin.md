# AssetPlugin

Configure and register the asset system into an `App`.

```cpp
#import epix.assets
```

---

## `AssetPlugin`

```cpp
struct AssetPlugin {
    std::string       file_path;              // default: "assets"
    std::string       processed_file_path;    // default: "imported_assets/Default"
    AssetServerMode   mode;                   // Unprocessed (default) | Processed
    AssetMetaCheck    meta_check;             // Never (default) | Always | Paths{…}
    UnapprovedPathMode unapproved_path_mode;  // Allow (default) | Deny | Forbid

    void build(App&);
    void finish(App&);

    // Register a custom asset source before build() is called
    AssetPlugin& register_asset_source(std::string id, AssetSourceBuilder);
};
```

`AssetPlugin::build()` inserts:
- `AssetServer` resource (backed by IOTaskPool)
- `AssetSources` resource (built from registered sources)
- Default OS filesystem source reading from `file_path`

`AssetPlugin::finish()` starts background load workers and activates watch streams.

### `AssetServerMode`

```cpp
enum class AssetServerMode { Unprocessed, Processed };
```

| Mode          | Description                                                    |
| ------------- | -------------------------------------------------------------- |
| `Unprocessed` | Loads raw files from `file_path` (default)                     |
| `Processed`   | Loads from `processed_file_path` (asset build pipeline output) |

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
