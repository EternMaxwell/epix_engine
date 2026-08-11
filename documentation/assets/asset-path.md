# Asset paths

`AssetPath` combines a source, filesystem path, and optional sub-asset label.

```cpp
import epix.assets;
using namespace epix::assets;
```

## Shape and parsing

```cpp
struct AssetPath {
    AssetSourceId source;
    std::filesystem::path path;
    std::optional<std::string> label;

    explicit AssetPath(std::string_view value);
    static std::optional<AssetPath> try_parse(std::string_view value);
    std::string string() const;
};
```

The textual form is `[source://]path[#label]`:

| Text | Source | Path | Label |
| --- | --- | --- | --- |
| `textures/icon.png` | default | `textures/icon.png` | none |
| `embedded://fonts/ui.ttf` | `embedded` | `fonts/ui.ttf` | none |
| `models/ship.glb#Hull` | default | `models/ship.glb` | `Hull` |

`AssetSourceId` derives from `std::optional<std::string>`. `is_default()` reports the empty/default source and `as_str()` returns `std::optional<std::string_view>`.

## Transforming paths

```cpp
AssetPath with_source(AssetSourceId source) const;
AssetPath with_label(std::string label) const;
AssetPath without_label() const;
void remove_label();
std::optional<std::string> take_label();

std::optional<AssetPath> parent() const;
AssetPath resolve(const AssetPath& relative) const;
AssetPath resolve_embed(const AssetPath& relative) const;
```

`resolve()` appends a relative path to the base. `resolve_embed()` applies embedded/RFC-style semantics by removing the base filename before joining; it does not change the source to `embedded` automatically.

Use `get_extension()`, `get_full_extension()`, and `iter_secondary_extensions()` for extension-aware loader selection. `is_unapproved()` detects paths that escape the configured asset root.

## Source registration

The default source normally reads from `AssetPlugin::file_path` (`assets`). Named sources are registered through the asset source builders used by `AssetPlugin`. A missing named source is reported as `MissingAssetSourceError` by operations such as `AssetServer::reload()`.
