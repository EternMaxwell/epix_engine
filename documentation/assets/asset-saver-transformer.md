# Asset Savers and Transformers

Write loadable assets back to bytes (`AssetSaver`) or convert between asset types (`AssetTransformer`).  
Both concepts are used by the asset processing pipeline.

```cpp
#import epix.assets
```

---

## `AssetSaver` concept

```cpp
template<typename T>
concept AssetSaver = requires {
    typename T::Asset;         // must satisfy Asset
    typename T::Settings;      // must satisfy is_settings (plain aggregate, zpp::bits-serializable)
    typename T::OutputLoader;  // an AssetLoader that can reload the saved bytes
    typename T::Error;

    { T::save(std::declval<std::ostream&>(),
              std::declval<SavedAsset<typename T::Asset>>(),
              std::declval<const typename T::Settings&>(),
              std::declval<const std::filesystem::path&>()) }
        -> std::convertible_to<
               std::expected<typename T::OutputLoader::Settings, typename T::Error>>;
};
```

A saver writes the asset to an `ostream` and returns the settings that the `OutputLoader` should
use when the file is read back. The returned settings are stored in the sidecar `.meta` file.

### Minimal example

```cpp
struct BinImageSaver {
    using Asset        = Image;
    struct Settings    {};                     // plain aggregate
    using OutputLoader = BinImageLoader;       // knows how to reload the file
    using Error        = std::exception_ptr;

    static std::expected<BinImageLoader::Settings, Error> save(
        std::ostream& out,
        SavedAsset<Image> asset,
        const Settings&,
        const std::filesystem::path& path)
    {
        const Image& img = asset.get();
        // write img bytes to out …
        return BinImageLoader::Settings{};     // settings for the output loader
    }
};
```

---

## `SavedAsset<A>`

Read-only view of an asset provided to savers and the processor pipeline.

```cpp
template<Asset A>
struct SavedAsset {
    const A& get() const;

    // Access labeled sub-assets (added via LoadContext)
    template<Asset B>
    const B&               get_labeled(std::string_view label) const;
    std::vector<std::string> labels() const;

    // Build from a LoadedAsset (for use in processors)
    static SavedAsset from_loaded(const LoadedAsset<A>&);
    static SavedAsset from_transformed(const TransformedAsset<A>&);
};
```

---

## `AssetTransformer` concept

```cpp
template<typename T>
concept AssetTransformer = requires {
    typename T::AssetInput;    // source asset type, must satisfy Asset
    typename T::AssetOutput;   // target asset type, must satisfy Asset
    typename T::Settings;      // must satisfy is_settings (plain aggregate, zpp::bits-serializable)
    typename T::Error;

    { T::transform(std::declval<TransformedAsset<typename T::AssetInput>>(),
                   std::declval<const typename T::Settings&>()) }
        -> std::convertible_to<
               std::expected<TransformedAsset<typename T::AssetOutput>, typename T::Error>>;
};
```

A transformer takes ownership of a `TransformedAsset<AssetInput>`, converts it, and returns a
`TransformedAsset<AssetOutput>`. The input type and output type may differ.

### Example: compress image to DXT

```cpp
struct DxtTransformer {
    using AssetInput  = RawImage;
    using AssetOutput = CompressedImage;
    struct Settings {
        int quality = 4;
    };
    using Error = std::exception_ptr;

    static std::expected<TransformedAsset<CompressedImage>, Error> transform(
        TransformedAsset<RawImage> input,
        const Settings& settings)
    {
        RawImage raw = std::move(input).get();
        CompressedImage out = compress_dxt(raw, settings.quality);
        return TransformedAsset<CompressedImage>::from(std::move(out));
    }
};
```

---

## `TransformedAsset<A>`

Mutable wrapper used inside transformer implementations.

```cpp
template<Asset A>
struct TransformedAsset {
    A&        get();
    const A&  get() const;

    // Replace the primary asset value
    template<Asset B>
    TransformedAsset<B> replace_asset(B new_value) &&;

    // Access or mutate labeled sub-assets
    template<Asset B>
    B*                     get_labeled(std::string_view label);
    std::vector<std::string> labels() const;
};
```

`TransformedAsset` is passed by value into `transform()`; the transformer can move from it to
produce a `TransformedAsset` of a different type.
