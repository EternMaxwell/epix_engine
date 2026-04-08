# AssetServer

Central async asset server. Loads, caches, hot-reloads, and tracks the lifecycle of all assets.

```cpp
#import epix.assets
```

---

## `AssetServer`

Available as a `Res<AssetServer>` resource after `AssetPlugin` is built.

### Loading

```cpp
// Async load — immediately returns a strong Handle; loading happens in background
template<Asset A>
Handle<A> load(AssetPath path);

// Load, ignoring any existing cached copy
template<Asset A>
Handle<A> load_override(AssetPath path);

// Async load with custom settings override
template<Asset A, typename S>
Handle<A> load_with_settings(AssetPath path, std::function<void(S&)> settings_fn);

// Blocking load — suspends the caller until the asset is ready
template<Asset A>
std::expected<Handle<A>, AssetLoadError> load_acquire(AssetPath path);

// Load all assets in a directory; returns a Handle<LoadedFolder>
Handle<LoadedFolder> load_folder(AssetPath path);

// Load without knowing the asset type
UntypedHandle load_untyped(AssetPath path);
```

### Inserting assets directly

```cpp
// Synchronously insert a value; returns a strong Handle
template<Asset A>
Handle<A> add(A asset);

// Insert a future; the asset is available once the future resolves
template<Asset A, typename E>
Handle<A> add_async(std::future<std::expected<A, E>> future);
```

### Hot-reload

```cpp
// Trigger a reload of all assets loaded from path
void reload(AssetPath path);
```

### Waiting

```cpp
// Await until asset and all dependencies are loaded (blocks caller)
template<Asset A>
std::expected<void, WaitForAssetError> wait_for_asset(Handle<A> handle);
```

### Load-state queries

```cpp
LoadState               get_load_state(UntypedAssetId id) const;
DependencyLoadState     get_dependency_load_state(UntypedAssetId id) const;
RecursiveDependencyLoadState
                        get_recursive_dependency_load_state(UntypedAssetId id) const;

bool is_loaded(UntypedAssetId id) const;
bool is_loaded_with_dependencies(UntypedAssetId id) const;
```

### Handle / path lookups

```cpp
template<Asset A>
Handle<A>               get_handle(AssetPath path) const;       // existing handle or invalid
template<Asset A>
Handle<A>               get_id_handle(AssetId<A> id) const;

std::optional<AssetPath> get_path(AssetId<A> id) const;
std::optional<AssetPath> get_path_untyped(UntypedAssetId id) const;
```

### Registration (called by `app_register_*` helpers; rarely needed directly)

```cpp
template<AssetLoader L>  void register_loader(L loader = {});
template<Asset A>        void register_asset();
template<AssetLoader L>  void preregister_loader(std::span<std::string_view> exts);
```

---

## Load States

### `LoadState`

```cpp
enum class LoadStateOK { NotLoaded, Loading, Loaded };
using LoadState = std::variant<LoadStateOK, AssetLoadError>;
```

| Value            | Meaning                                              |
| ---------------- | ---------------------------------------------------- |
| `NotLoaded`      | Never requested, or handle is invalid                |
| `Loading`        | I/O or decode in progress                            |
| `Loaded`         | Asset bytes decoded; sub-assets may still be loading |
| `AssetLoadError` | Load failed — inspect the variant for reason         |

### `DependencyLoadState` / `RecursiveDependencyLoadState`

```cpp
struct DependencyLoadState {
    bool is_loaded() const;
    bool is_loading() const;
    bool is_failed() const;
};
```

`DependencyLoadState` reflects the direct dependencies of the asset.  
`RecursiveDependencyLoadState` reflects the entire transitive dependency tree.  
Use `is_loaded_with_dependencies()` as a shorthand.

---

## `WaitForAssetError`

```cpp
namespace wait_for_asset_error {
    struct NotLoaded  {};              // handle is not associated with any load task
    struct Failed     { AssetLoadError error; };
    struct DependencyFailed { AssetLoadError error; };
}
using WaitForAssetError = std::variant<
    wait_for_asset_error::NotLoaded,
    wait_for_asset_error::Failed,
    wait_for_asset_error::DependencyFailed>;
```

---

## `AssetServerStats`

Lightweight snapshot of server activity.

```cpp
struct AssetServerStats {
    std::size_t started_load_tasks;    // total tasks ever started
    std::size_t finished_load_tasks;   // total tasks completed (success or fail)
};
```

```cpp
auto stats = server.stats();
float progress = float(stats.finished_load_tasks) / stats.started_load_tasks;
```

---

## `MissingAssetSourceError`

```cpp
struct MissingAssetSourceError { AssetSourceId source_id; };
```

Returned when a load request names a source that has not been registered.

---

## Example: loading with settings override

```cpp
Handle<Texture> tex = server.load_with_settings<Texture, TextureSettings>(
    "logo.png",
    [](TextureSettings& s) {
        s.format  = PixelFormat::Srgb;
        s.mipmaps = true;
    }
);
```

## Example: blocking load in a startup system

```cpp
app.add_systems(Startup, [](Res<AssetServer> server) {
    auto result = server->load_acquire<Shader>("shaders/pbr.slang");
    if (!result) {
        // AssetLoadError — log and abort
        std::terminate();
    }
    Handle<Shader> shader = std::move(*result);
    // shader is guaranteed fully loaded here
});
```
