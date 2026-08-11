# Asset handles and IDs

Assets are addressed by typed or type-erased IDs. Strong handles keep an asset alive; weak handles carry only an ID.

```cpp
import epix.assets;
using namespace epix::assets;
```

## IDs

`AssetIndex` is a generational storage key. Its fields are private; use `index()` and `generation()`. Values are allocated by `Assets<T>` and should not be constructed manually.

`AssetId<T>` contains either an `AssetIndex` or a stable `uuids::uuid`:

```cpp
bool is_index() const;
bool is_uuid() const;
std::string to_string() const;
std::string to_string_short() const;
static AssetId invalid();
```

`UntypedAssetId` adds a runtime `meta::type_index`. `typed<T>()` asserts that the stored type matches; `try_typed<T>()` returns `std::optional<AssetId<T>>`.

## `Handle<T>`

```cpp
bool is_strong() const;
bool is_weak() const;
AssetId<T> id() const;
std::optional<AssetPath> path() const;

Handle<T> weak() const;
void make_strong(Assets<T>& assets);
UntypedHandle untyped() const;
```

Copying a strong handle shares the reference-counted `StrongHandle`. Dropping the last strong copy allows the asset lifecycle system to emit `Unused` and reclaim the value. `weak()` does not keep the asset alive. `make_strong()` upgrades a weak handle in place when its asset exists in the supplied store; it does nothing when already strong.

```cpp
Handle<Image> strong = server.load<Image>("icon.png");
Handle<Image> weak = strong.weak();
weak.make_strong(images);
```

## `UntypedHandle`

```cpp
meta::type_index type_id() const;
meta::type_index type() const; // alias
UntypedAssetId id() const;
std::optional<AssetPath> path() const;
UntypedHandle weak() const;

template<Asset T>
std::expected<Handle<T>, UntypedAssetConversionError> try_typed() const;

template<Asset T>
Handle<T> typed() const; // throws on mismatch
```

`UntypedAssetConversionError` records the `expected` and `found` runtime type IDs.
