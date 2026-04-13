# Asset Loaders

Implement `AssetLoader` to decode a byte stream into a typed asset.

```cpp
#import epix.assets
```

---

## `AssetLoader` concept

```cpp
template<typename T>
concept AssetLoader = requires {
    typename T::Asset;        // must satisfy Asset
    typename T::Settings;     // must satisfy is_settings (plain aggregate, zpp::bits-serializable)
    typename T::Error;        // error type (e.g. std::exception_ptr)

    // Which file extensions this loader handles
    { T::extensions() } -> std::convertible_to<std::span<std::string_view>>;

    // Decode stream into T::Asset
    { T::load(std::declval<std::istream&>(),
              std::declval<const T::Settings&>(),
              std::declval<LoadContext&>()) }
        -> std::convertible_to<std::expected<typename T::Asset, typename T::Error>>;
};
```

### Minimal loader

```cpp
struct TextLoader {
    using Asset    = std::string;
    struct Settings {};          // plain aggregate — no inheritance needed
    using Error    = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static std::array exts = { std::string_view{"txt"}, std::string_view{"text"} };
        return exts;
    }

    static std::expected<std::string, Error> load(
        std::istream& stream,
        const Settings&,
        LoadContext&)
    {
        std::ostringstream ss;
        ss << stream.rdbuf();
        return ss.str();
    }
};
```

Register with:

```cpp
app_register_asset<std::string>(app);
app_register_loader<TextLoader>(app);
```

---

## `LoadContext`

Passed by the framework to every `AssetLoader::load()` call.

```cpp
struct LoadContext {
    // The path being loaded
    AssetPath path() const;

    // Access the AssetServer (for manual sub-asset loading)
    AssetServer& asset_server();

    // Record a dependency on another asset
    void track_dependency(UntypedHandle handle);

    // Register a labeled sub-asset
    template<Asset A>
    Handle<A> add_labeled_asset(std::string label, A asset);

    template<Asset A>
    Handle<A> add_loaded_labeled_asset(std::string label, LoadedAsset<A> loaded);

    // Check if a label was already registered
    bool has_labeled_asset(std::string_view label) const;

    // Build a NestedLoader for loading from within this context
    NestedLoader loader();

    // Directly load another asset synchronously (blocks the IOTaskPool thread)
    template<Asset A>
    std::expected<A, AssetLoadError> load_direct(AssetPath path);

    // Load a labeled sub-asset inline with a callback
    template<Asset A>
    Handle<A> labeled_asset_scope(std::string label, auto&& make);

    // Finalise — wrap the value into a LoadedAsset
    template<Asset A>
    LoadedAsset<A> finish(A value);
};
```

### Loading a sub-asset via nested loader

```cpp
static std::expected<Scene, Error> load(
    std::istream& stream, const Settings&, LoadContext& ctx)
{
    // Load a texture referenced inside the scene file
    Handle<Image> tex = ctx.loader()
        .with_settings<ImageSettings>([](auto& s){ s.srgb = true; })
        .load<Image>("textures/diffuse.png");

    ctx.track_dependency(tex.untyped());
    return Scene{ .diffuse = tex };
}
```

---

## `NestedLoader`

Builder returned by `LoadContext::loader()`.

```cpp
struct NestedLoader {
    // Override loader settings for the nested load
    template<typename S>
    NestedLoader with_settings(std::function<void(S&)> settings_fn) &&;

    // Load typed asset relative to the parent path
    template<Asset A>
    Handle<A> load(AssetPath path) &&;

    // Load without type
    UntypedHandle load_untyped(AssetPath path) &&;

    // Resolve path relative to parent and load
    template<Asset A>
    Handle<A> load_relative(std::string_view relative_path) &&;
};
```

---

## `LoadedAsset<A>`

Typed loaded asset that may contain labeled sub-assets.

```cpp
template<Asset A>
struct LoadedAsset {
    const A& get() const;
    A        take() &&;

    // Labeled sub-assets added via LoadContext::add_labeled_asset
    std::vector<std::string>    labels() const;
    ErasedLoadedAsset*          get_labeled(std::string_view label);
    const ErasedLoadedAsset*    get_labeled(std::string_view label) const;
};
```

---

## `ErasedLoadedAsset`

Type-erased `LoadedAsset`. Used internally; also exposed to savers and cross-type queries.

```cpp
struct ErasedLoadedAsset {
    meta::type_index asset_type_id() const;

    template<Asset A> A*       get();
    template<Asset A> A        take() &&;
    template<Asset A> LoadedAsset<A> downcast() &&;

    // Build from a typed value
    template<Asset A>
    static ErasedLoadedAsset from_asset(A value);
};
```

---

## Load Error Types

```cpp
namespace load_error {
    struct RequestHandleMismatch { meta::type_index expected; meta::type_index found; };
    struct MissingAssetLoader    { AssetPath path; };
    struct AssetLoaderException  { std::exception_ptr error; };
}
using AssetLoadError = std::variant<
    load_error::RequestHandleMismatch,
    load_error::MissingAssetLoader,
    load_error::AssetLoaderException>;
```
