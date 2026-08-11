# Asset Loaders

```cpp
import epix.assets;
```

An `AssetLoader` asynchronously decodes a `Reader` into a movable asset.

```cpp
struct TextLoader {
    using Asset = std::string;
    struct Settings {};
    using Error = std::exception_ptr;

    std::span<std::string_view> extensions() const {
        static constexpr std::array extensions{"txt"sv, "text"sv};
        return extensions;
    }

    STDEXEC::task<std::expected<Asset, Error>> load(
        Reader& reader, const Settings&, LoadContext&) const
    {
        std::vector<std::uint8_t> bytes;
        auto result = co_await reader.read_to_end(bytes);
        if (!result) {
            co_return std::unexpected(
                std::make_exception_ptr(std::system_error(result.error())));
        }
        co_return std::string(bytes.begin(), bytes.end());
    }
};
```

`Settings` must be default-constructible and satisfy `is_settings`. The loader
error type is converted to `std::exception_ptr` by
`asset_loader_error_to_exception` (the generic implementation wraps it).

Register the loader and its asset type with `app_register_asset<T>(app)` and
`app_register_loader<Loader>(app)`.

## LoadContext

`LoadContext` exposes the current `AssetPath`, the `AssetServer`, dependency
tracking, labeled sub-assets, and a `NestedLoader`. `finish(value)` moves the
context's dependencies and labels into a `LoadedAsset<T>`.

Direct loading is asynchronous:

```cpp
auto loaded = co_await context.load_direct<Image>(path);
auto loaded_with_reader =
    co_await context.load_direct_with_reader<Image>(path, custom_reader);
```

Both return `std::expected<LoadedAsset<Image>, AssetLoadError>`.

## NestedLoader

`context.loader()` returns a builder that can apply a one-shot settings
transform and create typed or untyped dependency handles:

```cpp
Handle<Image> texture = context.loader()
    .with_settings<ImageSettings>([](auto& settings) { settings.srgb = true; })
    .load_relative<Image>("textures/diffuse.png");
```

`load`, `load_untyped`, and `load_relative` register the returned handle as a
dependency of the parent load.

## LoadedAsset

`LoadedAsset<T>` owns the loaded value plus dependency handles and labeled
assets. It provides typed access/take operations, label iteration, optional
reference access to labeled assets, and conversion to the internal
`ErasedLoadedAsset` representation.
