# Asset Savers and Transformers

```cpp
import epix.assets;
```

Savers write loaded assets to asynchronous `Writer` streams. Transformers
convert one loaded asset type into another. Both are used by asset processing.

## AssetSaver

An asset saver declares `Asset`, `Settings`, `OutputLoader`, and `Error`, then
implements:

```cpp
STDEXEC::task<std::expected<OutputLoader::Settings, Error>> save(
    Writer& writer,
    SavedAsset<Asset> asset,
    const Settings& settings,
    const AssetPath& output_path) const;
```

The returned settings are used by `OutputLoader` when the saved bytes are read
again.

```cpp
struct ImageSaver {
    using Asset = Image;
    struct Settings {};
    using OutputLoader = ImageLoader;
    using Error = std::exception_ptr;

    STDEXEC::task<std::expected<OutputLoader::Settings, Error>> save(
        Writer& writer, SavedAsset<Image> image,
        const Settings&, const AssetPath&) const
    {
        auto written = co_await writer.write(image->pixels);
        if (!written) {
            co_return std::unexpected(
                std::make_exception_ptr(std::system_error(written.error())));
        }
        auto flushed = co_await writer.flush();
        if (!flushed) {
            co_return std::unexpected(
                std::make_exception_ptr(std::system_error(flushed.error())));
        }
        co_return OutputLoader::Settings{};
    }
};
```

`SavedAsset<T>` is a non-owning, read-only view. It supports dereference,
`get()`, optional typed/untyped labeled-asset access, handle lookup, and label
iteration. Construct it with `from_loaded`, `from_transformed`, or `from_asset`.

## AssetTransformer

A transformer declares `AssetInput`, `AssetOutput`, `Settings`, and `Error`, and
returns an asynchronous transformed value:

```cpp
STDEXEC::task<std::expected<TransformedAsset<AssetOutput>, Error>> transform(
    TransformedAsset<AssetInput> input,
    const Settings& settings) const;
```

`TransformedAsset<T>` owns the asset and carries labeled assets across the
transformation. Use its factory/access/take operations rather than discarding
the wrapper when labels must survive.
