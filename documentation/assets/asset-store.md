# Asset Store

Typed in-memory collection of loaded assets and their lifecycle events.

```cpp
#import epix.assets
```

---

## `Assets<T>`

```cpp
template<Asset T>
struct Assets {
    // --- insertion ---
    Handle<T> add(T asset);
    template<typename... Args> Handle<T> emplace(Args&&... args);  // in-place construct
    Handle<T> reserve_handle();                                    // allocate id before data is ready
    Handle<T> get_or_insert_with(AssetId<T> id, auto&& make);     // insert if absent

    // --- lookup ---
    const T* get(AssetId<T> id) const;
    T*       get_mut(AssetId<T> id);
    std::expected<const T*, AssetError> try_get(AssetId<T> id) const;
    std::expected<T*, AssetError>       try_get_mut(AssetId<T> id);

    // --- removal ---
    std::optional<T> remove(AssetId<T> id);  // emits Removed event
    std::optional<T> take(AssetId<T> id);    // removes without event

    // --- untracked variants (no event) ---
    T*       get_mut_untracked(AssetId<T> id);
    std::optional<T> remove_untracked(AssetId<T> id);

    // --- query ---
    bool     contains(AssetId<T> id) const;
    bool     is_empty() const;
    std::size_t len() const;

    // iteration (ids only; use get() for values)
    auto ids()      const;  // range of AssetId<T>
    auto iter()     const;  // range of const T&
    auto iter_mut();        // range of T&

    // --- system callbacks (provided to App by app_register_asset) ---
    static void handle_events(Res<Assets<T>>, EventReader<internal_asset_event::…>);
    static void asset_events(Res<Assets<T>>,  EventWriter<AssetEvent<T>>, …);
};
```

### `add()` vs `emplace()`

```cpp
Assets<std::string>& texts = …;
Handle<std::string> h1 = texts.add(std::string{"hello"});
Handle<std::string> h2 = texts.emplace("world");  // no copy
```

### `get_or_insert_with()`

Inserts a default value the first time an id is seen; returns a handle in both cases.

```cpp
Handle<Mesh> cube = meshes.get_or_insert_with(cube_id, [] { return Mesh::cube(); });
```

---

## `AssetEvent<T>`

Lifecycle event emitted via `EventWriter` after each `WriteEvents` tick.

```cpp
template<Asset T>
struct AssetEvent {
    enum class Type { Added, Removed, Modified, Unused, LoadedWithDependencies };
    Type        kind;
    AssetId<T>  id;

    bool is_added() const;
    bool is_removed() const;
    bool is_modified() const;
    bool is_unused() const;
    bool is_loaded_with_dependencies() const;

    // static factory methods
    static AssetEvent added(AssetId<T> id);
    static AssetEvent removed(AssetId<T> id);
    static AssetEvent modified(AssetId<T> id);
    static AssetEvent unused(AssetId<T> id);
    static AssetEvent loaded_with_dependencies(AssetId<T> id);
};
```

| Event                    | When                                                        |
| ------------------------ | ----------------------------------------------------------- |
| `Added`                  | Asset inserted (by loader or manually via `add()`)          |
| `Removed`                | Asset removed from `Assets<T>`                              |
| `Modified`               | Asset value replaced in-place (hot-reload or user mutation) |
| `Unused`                 | Last strong `Handle<T>` was dropped                         |
| `LoadedWithDependencies` | Asset and all recursive dependencies finished loading       |

```cpp
app.add_systems(Update, [](EventReader<AssetEvent<Image>> events) {
    for (auto& e : events.read()) {
        if (e.is_loaded_with_dependencies())
            std::println("ready: {}", e.id.to_string_short());
    }
});
```

---

## `AssetLoadFailedEvent<T>` / `UntypedAssetLoadFailedEvent`

Emitted when a load attempt fails.

```cpp
template<Asset T>
struct AssetLoadFailedEvent {
    AssetId<T>      id;
    AssetPath       path;
    AssetLoadError  error;
};

struct UntypedAssetLoadFailedEvent {
    UntypedAssetId  id;
    AssetPath       path;
    AssetLoadError  error;
};
```

---

## `LoadedFolder`

Returned by `AssetServer::load_folder()`. Contains handles for all assets found in the directory.

```cpp
struct LoadedFolder {
    std::vector<UntypedHandle> handles;
};
```

---

## `LoadedUntypedAsset`

Returned by `AssetServer::load_untyped()`.

```cpp
struct LoadedUntypedAsset {
    UntypedHandle handle;
};
```

---

## Error Types

```cpp
struct IndexOutOfBound { AssetIndex index; };
struct SlotEmpty       { AssetIndex index; };
struct GenMismatch     { AssetIndex stored; AssetIndex requested; };
struct AssetNotPresent { std::variant<AssetIndex, uuids::uuid> id; };

using AssetError = std::variant<
    IndexOutOfBound, SlotEmpty, GenMismatch, AssetNotPresent>;
```

Returned by `Assets<T>::try_get()` / `try_get_mut()` on lookup failure.
