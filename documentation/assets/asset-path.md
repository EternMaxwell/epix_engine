# Asset Paths

Asset addresses that combine a source, a path, and an optional sub-asset label.

```cpp
#import epix.assets
```

---

## `AssetSourceId`

Optional name identifying which registered source to read from.

```cpp
struct AssetSourceId {
    bool        is_default() const;   // true when no source name is stored
    std::string_view as_str() const;  // "" for the default source
};
```

The default source corresponds to the filesystem root set in `AssetPlugin::file_path` (`"assets/"` by default). Named sources are registered via `AssetPlugin::register_asset_source()`.

---

## `AssetPath`

Full address for a single asset or labeled sub-asset.

```cpp
struct AssetPath {
    // Syntax: [source_id "://"] path ["#" label]
    // Examples:
    //   "sprite.png"
    //   "textures/terrain.png#albedo"
    //   "embedded://font.ttf"

    AssetSourceId           source() const;
    const std::filesystem::path& path() const;
    std::optional<std::string>   label() const;

    // Fluent builders
    AssetPath with_source(std::string_view source) const;
    AssetPath with_label(std::string_view label) const;

    // Resolve a relative path against this path's parent directory
    AssetPath resolve(std::string_view relative) const;

    // Like resolve(), but forces the "embedded://" source
    AssetPath resolve_embed(std::string_view relative) const;

    // Parent directory path (without label)
    AssetPath parent() const;
};
```

`AssetPath` is constructible from `std::string_view` and `std::filesystem::path`. The constructor parses the `source://path#label` syntax:

```cpp
AssetPath full("textures://terrain.png#albedo");
// full.source()  → AssetSourceId{"textures"}
// full.path()    → "terrain.png"
// full.label()   → std::optional{"albedo"}

AssetPath relative = full.resolve("normal.png");
// → AssetPath{"textures://normal.png"}  (same source, label stripped)
```

### Parsing rules

| Input                   | Source       | Path            | Label    |
| ----------------------- | ------------ | --------------- | -------- |
| `"icon.png"`            | default      | `"icon.png"`    | none     |
| `"ui/icon.png#glow"`    | default      | `"ui/icon.png"` | `"glow"` |
| `"embedded://font.ttf"` | `"embedded"` | `"font.ttf"`    | none     |
| `"http://cdn/img.png"`  | `"http"`     | `"cdn/img.png"` | none     |
