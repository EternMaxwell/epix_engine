# Asset server

`AssetServer` is the thread-safe entry point for loading, tracking, reloading, and directly inserting assets.

```cpp
import epix.assets;
using namespace epix::assets;
```

`AssetPlugin` inserts it as a resource during plugin attachment.

## Loading

The ordinary load methods return a strong handle immediately. File I/O and decoding continue on the asset task pipeline.

```cpp
Handle<Image> image = server.load<Image>("textures/icon.png");
Handle<Image> forced = server.load_override<Image>("textures/icon.png");

Handle<Image> configured = server.load_with_settings<Image, ImageSettings>(
    "textures/icon.png",
    [](ImageSettings& settings) { settings.srgb = true; });

UntypedHandle inferred = server.load_untyped("data/item.custom");
Handle<LoadedFolder> folder = server.load_folder("textures");
```

The corresponding `*_override` methods force a new load and permit an otherwise unapproved path.
`load_with_meta_transform()` is the lower-level typed entry point used by those overloads.
`load_erased(type_id, path)` is available when the type is known only at runtime.

`load_acquire<A>()` and `load_acquire_with_settings<A, S>()` block until the asset and recursive dependencies finish, then return `Handle<A>`. They intentionally discard the detailed wait result; call `load()` followed by `wait_for_asset()` when failure handling matters. Never block the thread that runs the asset event handler.

```cpp
auto handle = server.load<Image>("textures/icon.png");
if (auto ready = server.wait_for_asset(handle); !ready) {
    // inspect ready.error()
}
```

## Direct insertion

```cpp
Handle<Mesh> immediate = server.add(Mesh{/* ... */});

Handle<Mesh> pending = server.add_async<Mesh, BuildError>([]() -> std::expected<Mesh, BuildError> {
    return build_mesh();
});
```

`add_async` schedules the producer on `IoTaskPool`; the producer itself is synchronous and returns `std::expected<A, E>`.

## Reloading and state

```cpp
std::expected<void, MissingAssetSourceError> reload(const AssetPath& path) const;

std::optional<LoadState> get_load_state(const UntypedAssetId& id) const;
std::optional<DependencyLoadState> get_dependency_load_state(const UntypedAssetId& id) const;
std::optional<RecursiveDependencyLoadState>
get_recursive_dependency_load_state(const UntypedAssetId& id) const;

bool is_loaded(const UntypedAssetId& id) const;
bool is_loaded_with_direct_dependencies(const UntypedAssetId& id) const;
bool is_loaded_with_dependencies(const UntypedAssetId& id) const;
```

`LoadState` is `std::variant<LoadStateOK, std::shared_ptr<AssetLoadError>>`, where `LoadStateOK` is `NotLoaded`, `Loading`, or `Loaded`. The dependency state types provide `is_loaded()`, `is_loading()`, and `is_failed()` helpers.

`wait_for_asset()` returns `std::expected<void, WaitForAssetError>`. The error variant distinguishes an untracked asset, failure of the requested asset, and recursive dependency failure.

The type-erased equivalents are `wait_for_asset_untyped(handle)` and
`wait_for_asset_id(id)`. `get_load_states(id)` retrieves all three state values together. The
non-optional `load_state()`, `dependency_load_state()`, and
`recursive_dependency_load_state()` variants return their respective default state when the id is
not tracked.

## Handles and paths

```cpp
auto by_path = server.get_handle<Image>("textures/icon.png");
auto by_id = server.get_id_handle(image.id());
auto erased = server.get_handle_untyped("textures/icon.png");
auto all_for_path = server.get_handles_untyped("textures/icon.png");
auto erased_by_id = server.get_id_handle_untyped(erased->id());

std::optional<AssetPath> path = server.get_path(UntypedAssetId{image.id()});
std::optional<UntypedAssetId> id = server.get_path_id("textures/icon.png");
std::vector<UntypedAssetId> ids = server.get_path_ids("textures/icon.png");
```

`get_or_create_path_handle<A>()` and `get_or_create_path_handle_erased()` create tracked handles
without starting a load. `get_path_and_type_id_handle()` disambiguates a path shared by several
asset types. `is_managed(id)` reports whether the server tracks an id.

Loader and asset registration are normally performed by `app_register_asset`,
`app_register_loader`, and related helpers rather than by application code.

## Source policy

`reload()` reports `MissingAssetSourceError` if the path names an unregistered source. Unapproved paths are governed by `UnapprovedPathMode`; see [Asset meta](asset-meta.md).

Use `mode()` and `watching_for_changes()` to inspect the active configuration, and
`get_source(source_id)` to inspect a configured source. Loader lookup is available by extension,
loader type name, path, or asset type through `get_asset_loader_with_extension()`,
`get_asset_loader_with_type_name()`, `get_path_asset_loader()`, and
`get_asset_loader_with_asset_type[_id]()`. These lookup and registration functions are primarily
plugin/loader integration APIs.

## Direct uncached loading

`load_direct_untyped(path)` and `load_direct_with_reader_untyped(path, reader)` are coroutine APIs
used by nested loaders. They return an `expected<ErasedLoadedAsset, AssetLoadError>` without
registering a normal path handle in the server. `NestedLoader::load_direct<A>()` provides the typed
front end. Prefer ordinary `load()` for application-owned assets so caching, handle lifetime,
reloads, and lifecycle events remain active.

`load_asset_untyped()` and `process_handle_destruction()` are low-level integration hooks used by
the asset pipeline and store event handler; normal application code should not need them.
