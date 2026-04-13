# Shader Import and References

Types that identify which shader to import and how to refer to a shader from a pipeline.

## `ShaderImport`

### Overview

An import target: either a concrete asset path (a file) or a custom module name.

| Variant     | Stored as                            | Example source                       |
| ----------- | ------------------------------------ | ------------------------------------ |
| Asset path  | `assets::AssetPath`                  | `#import "/shared/math.wgsl"`        |
| Custom name | `std::filesystem::path` (normalized) | `#import ui::button` → `"ui/button"` |

Custom-name separators `::`, `.`, `/`, and `\` are all normalized to `/`.

### Usage

```cpp
// Build programmatically
auto file_import   = ShaderImport::asset_path(AssetPath("shaders/math.wgsl"));
auto name_import   = ShaderImport::custom("ui/button");

// Discriminate
if (import.is_custom()) {
    std::string name = import.as_custom();       // e.g. "ui/button"
} else {
    AssetPath path = import.as_asset_path();     // e.g. "shaders/math.wgsl"
}

// Canonical key used in composer/cache lookups
std::string key = import.module_name();
// custom import  → "ui/button"
// file import    → "\"shaders/math.wgsl\"" (quoted)
```

`ShaderImport` values are produced automatically by `Shader::preprocess()` and
`Shader::preprocess_slang()` when a `Shader` is loaded.

### Import resolution rules

Both WGSL and Slang follow the same four cases (illustrated with WGSL; Slang uses `import`
instead of `#import`):

| Source                                 | Resolved as                                      |
| -------------------------------------- | ------------------------------------------------ |
| `#import some::mod`                    | Custom — stored as `some/mod`                    |
| `#import "source://path/to/file.wgsl"` | File — uses the explicit source and path         |
| `#import "/path/to/file.wgsl"`         | File — relative to the source root               |
| `#import "common/file.wgsl"`           | File — relative to the importing shader's folder |

---

## `ShaderRef`

### Overview

Flexible reference to a shader from a pipeline or component. Avoids requiring a loaded
handle at construction time.

| Variant               | Meaning                                     |
| --------------------- | ------------------------------------------- |
| `ShaderRef::Default`  | Placeholder for "use the built-in default". |
| `ShaderRef::ByHandle` | Concrete loaded `Handle<Shader>`.           |
| `ShaderRef::ByPath`   | A path to resolve later.                    |

### Usage

```cpp
// Default (built-in shader)
ShaderRef ref;                  // default-constructed → Default variant

// From a loaded handle
ShaderRef ref = ShaderRef::from_handle(handle);

// From a path string
ShaderRef ref = ShaderRef::from_str("shaders/my_shader.wgsl");
ShaderRef ref = ShaderRef::from_path(std::filesystem::path{"shaders/my_shader.wgsl"});

// Discriminate
if (ref.is_default()) { /* ... */ }
if (ref.is_handle())  { /* use std::get<ShaderRef::ByHandle>(ref.value).handle */ }
if (ref.is_path())    { /* use std::get<ShaderRef::ByPath>(ref.value).path */ }
```

`ShaderRef` is a convenience wrapper used by render subsystems that accept flexible
shader inputs. `ShaderCache` operates on `AssetId<Shader>` directly.
