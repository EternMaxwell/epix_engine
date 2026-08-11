# Asset store

`Assets<T>` is the typed in-memory store for one asset type. `app_register_asset<T>()` installs the store and its events as ECS resources.

```cpp
import epix.assets;
using namespace epix::assets;
```

## Core API

```cpp
Handle<T> add(T asset);
template<class... Args> Handle<T> emplace(Args&&... args);
Handle<T> reserve_handle();

template<class... Args>
std::expected<bool, AssetError> insert(const AssetId<T>& id, Args&&... args);
std::expected<Handle<T>, AssetError> get_strong_handle(const AssetId<T>& id);

std::optional<std::reference_wrapper<const T>> get(const AssetId<T>& id) const;
std::optional<std::reference_wrapper<T>> get_mut(const AssetId<T>& id);

std::expected<std::reference_wrapper<const T>, AssetError>
try_get(const AssetId<T>& id) const;
std::expected<std::reference_wrapper<T>, AssetError>
try_get_mut(const AssetId<T>& id);

std::expected<void, AssetError> remove(const AssetId<T>& id);
std::expected<T, AssetError> take(const AssetId<T>& id);

template<class F>
std::expected<std::reference_wrapper<T>, AssetError>
get_or_insert_with(const AssetId<T>& id, F&& make);
```

Mutable tracked access records a `Modified` event. `get_mut_untracked()` and `remove_untracked()` are available for internal-style operations that must not emit the usual event.

```cpp
if (auto image = images.get_mut(handle.id())) {
    image->get().regenerate_mips();
}

auto mesh = meshes.get_or_insert_with(id, [] { return Mesh{/* ... */}; });
if (mesh) mesh->get().recalculate_bounds();
```

Use `contains()`, `is_empty()`, and `len()` for store queries. Iteration is callback-based:

```cpp
images.iter([](AssetId<Image> id, const Image& image) { /* ... */ });
images.iter_mut([](AssetId<Image> id, Image& image) { /* emits Modified */ });
```

## Lifecycle events

```cpp
template<Asset T>
struct AssetEvent {
    enum class Type { Added, Removed, Modified, Unused, LoadedWithDependencies } type;
    AssetId<T> id;

    bool is_added() const;
    bool is_removed() const;
    bool is_modified() const;
    bool is_unused() const;
    bool is_loaded_with_dependencies() const;
};
```

`Unused` means the last strong handle was dropped. `LoadedWithDependencies` means the asset and all recursive dependencies completed successfully.

```cpp
void observe(EventReader<AssetEvent<Image>> events) {
    for (const auto& event : events.read()) {
        if (event.is_loaded_with_dependencies()) {
            // event.id is ready
        }
    }
}
```

Load failures use `AssetLoadFailedEvent<T>` or `UntypedAssetLoadFailedEvent`. Both contain `id`, `path`, and an `error` stored as `std::variant<std::string, std::exception_ptr>`.

## Errors

```cpp
struct IndexOutOfBound { std::uint32_t index; };
struct SlotEmpty       { std::uint32_t index; };
struct GenMismatch {
    std::uint32_t index;
    std::uint32_t current_gen;
    std::uint32_t expected_gen;
};
using AssetNotPresent = std::variant<AssetIndex, uuids::uuid>;
using AssetError = std::variant<IndexOutOfBound, SlotEmpty, GenMismatch, AssetNotPresent>;
```

`LoadedFolder`, returned through the handle from `AssetServer::load_folder()`, contains `std::vector<UntypedHandle> handles`.

## Complete store utilities

- `ids()` returns a snapshot of all index- and UUID-backed ids.
- `reserve_handle()` reserves an index and returns its strong handle before a value exists;
  `insert(id, ...)` can populate it later.
- `get_strong_handle(id)` increments the store reference count and returns a strong handle for an
  existing asset.
- `get_handle_provider()` exposes the shared provider used for handle creation. It is mainly an
  asset-server integration API.
- `handle_events_manual(asset_server)` processes queued strong-handle lifecycle events. Registered
  apps run the equivalent store systems automatically.

`Assets<T>` supports both generational `AssetIndex` ids and stable UUID ids. The `insert` return
value is `true` when an existing value was replaced and `false` for a newly populated slot. A
tracked insert emits `Modified` or `Added` accordingly.

`LoadedUntypedAsset` stores one `UntypedHandle` and is the built-in asset type used to retain a
type-erased result.
