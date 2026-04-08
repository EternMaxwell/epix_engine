# Asset Handles and IDs

Typed and type-erased identifiers and handles for referencing assets.

```cpp
#import epix.assets
```

---

## `AssetIndex`

Internal generational slot index. Holds an asset's position and generation counter inside `Assets<T>`.

```cpp
struct AssetIndex {
    uint32_t index;
    uint32_t generation;
};
```

`AssetIndex` values are assigned by `Assets<T>` and exposed via `AssetId<T>`. Do not construct them manually.

---

## `AssetId<T>`

```cpp
template<Asset T>
struct AssetId {
    // Identity
    bool is_index() const;          // backed by AssetIndex
    bool is_uuid() const;           // backed by uuids::uuid

    std::string to_string() const;
    std::string to_string_short() const;

    static AssetId invalid();       // sentinel invalid id
    bool operator==(const AssetId&) const;
};
```

Variant of `AssetIndex` (slot-based, fast lookup) or `uuids::uuid` (stable across sessions).

```cpp
Handle<Image> h = server.load<Image>("icon.png");
AssetId<Image> id = h.id();
bool fast = id.is_index();   // true for most loaded assets
```

---

## `UntypedAssetId`

Type-erased version of `AssetId`. Carries both the id and the type's `meta::type_index`.

```cpp
struct UntypedAssetId {
    meta::type_index type_id() const;

    // downcast
    template<Asset T> AssetId<T>         typed() const;   // asserts type matches
    template<Asset T> std::optional<AssetId<T>> try_typed() const;
};
```

---

## `Handle<T>`

A typed reference to an asset. Can be **strong** (keeps the asset alive) or **weak** (does not).

```cpp
template<Asset T>
struct Handle {
    bool is_strong() const;
    bool is_weak() const;

    AssetId<T>               id() const;
    std::optional<AssetPath> path() const;

    Handle<T>   weak() const;                    // downgrade to weak copy
    Handle<T>   make_strong(Assets<T>& assets);  // upgrade weak handle

    UntypedHandle untyped() const;               // erase type

    // dependency tracking (used by loaders)
    void visit_dependencies(auto&& visitor) const;
};
```

Handles are obtained from `AssetServer::load<T>()` or `Assets<T>::reserve_handle()`. Cloning a strong handle increments the ref-count of the underlying `StrongHandle`; dropping the last strong Handle allows the asset to be removed on the next `HandleEvents` run.

```cpp
Handle<Image> a = server.load<Image>("tex.png");
Handle<Image> b = a;          // strong — both keep asset alive
Handle<Image> w = a.weak();   // weak — does not keep asset alive
```

---

## `UntypedHandle`

Type-erased Handle. Used where asset type is not known at compile time.

```cpp
struct UntypedHandle {
    meta::type_index type() const;
    UntypedAssetId   id() const;

    template<Asset T> Handle<T>                        typed() const;      // asserts type
    template<Asset T> std::expected<Handle<T>, UntypedAssetConversionError> try_typed() const;
};
```

```cpp
UntypedHandle u = server.load_untyped("unknown.bin");
if (u.type() == meta::type_id<Image>())
    Handle<Image> h = u.typed<Image>();
```

---

## `UntypedAssetConversionError`

Returned by `UntypedHandle::try_typed<T>()` when the stored type does not match `T`.

```cpp
struct UntypedAssetConversionError {
    meta::type_index expected;
    meta::type_index found;
};
```
