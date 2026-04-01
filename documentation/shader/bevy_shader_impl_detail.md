# Bevy Shader Module — Implementation Detail Reference

> Source crate: `bevy_shader` (`crates/bevy_shader/src/`)  
> Reference for C++ implementation in `epix_engine`  
> Files: `lib.rs`, `shader.rs`, `shader_cache.rs`

---

## Table of Contents

- [Bevy Shader Module — Implementation Detail Reference](#bevy-shader-module--implementation-detail-reference)
  - [Table of Contents](#table-of-contents)
  - [1. ShaderId](#1-shaderid)
  - [2. ShaderDefVal](#2-shaderdefval)
    - [`ShaderDefVal::value_as_string`](#shaderdefvalvalue_as_string)
  - [3. ValidateShader](#3-validateshader)
  - [4. Source](#4-source)
    - [`Source::as_str`](#sourceas_str)
  - [5. ShaderImport](#5-shaderimport)
    - [`ShaderImport::module_name`](#shaderimportmodule_name)
  - [6. Shader](#6-shader)
    - [`Shader::preprocess` (private)](#shaderpreprocess-private)
    - [`Shader::from_wgsl`](#shaderfrom_wgsl)
    - [`Shader::from_wgsl_with_defs`](#shaderfrom_wgsl_with_defs)
    - [`Shader::from_spirv`](#shaderfrom_spirv)
  - [7. ShaderSettings](#7-shadersettings)
  - [8. ShaderLoaderError](#8-shaderloadererror)
  - [9. ShaderLoader](#9-shaderloader)
  - [10. ShaderRef](#10-shaderref)
  - [11. CachedPipelineId](#11-cachedpipelineid)
  - [12. ShaderData (internal)](#12-shaderdata-internal)
  - [13. ShaderCacheSource](#13-shadercachesource)
  - [14. ShaderCacheError](#14-shadercacheerror)
  - [15. ShaderCache](#15-shadercache)
    - [`ShaderCache` constructor](#shadercache-constructor)
    - [`ShaderCache::add_import_to_composer` (private, static)](#shadercacheadd_import_to_composer-private-static)
    - [`ShaderCache::clear` (private)](#shadercacheclear-private)
    - [`ShaderCache::get`](#shadercacheget)
    - [`ShaderCache::set_shader`](#shadercacheset_shader)
    - [`ShaderCache::remove`](#shadercacheremove)
  - [16. ComposeError and ShaderComposer](#16-composeerror-and-shadercomposer)
    - [ComposeError](#composeerror)
    - [ShaderComposer](#shadercomposer)
    - [`ShaderComposer::add_module`](#shadercomposeradd_module)
    - [`ShaderComposer::remove_module`](#shadercomposerremove_module)
    - [`ShaderComposer::contains_module`](#shadercomposercontains_module)
    - [`ShaderComposer::compose`](#shadercomposercompose)
  - [17. EPIX\_LOAD\_SHADER\_LIBRARY (macro)](#17-epix_load_shader_library-macro)

---

## 1. ShaderId

```cpp
struct ShaderId {
private:
    static std::atomic<uint32_t> s_counter;
    uint32_t value;
    explicit ShaderId(uint32_t v) : value(v) {}
public:
    static ShaderId next();       // atomically increments s_counter
    uint32_t get() const { return value; }
    auto operator<=>(const ShaderId&) const = default;
};
template<>
struct std::hash<ShaderId> {
    std::size_t operator()(const ShaderId& id) const noexcept {
        return std::hash<uint32_t>{}(id.get());
    }
};
```

**Kind:** Newtype wrapper over an atomic `uint32_t` counter.

**Purpose:** Uniquely identifies a `Shader` object at runtime. Each new `Shader` instance receives a fresh ID by atomically incrementing a global counter.

**Operators/traits provided:**  
Three-way comparison (`operator<=>`), `std::hash` specialization, copy/move trivially copyable.

**API:**

| Method | Signature                | Description                                                    |
| ------ | ------------------------ | -------------------------------------------------------------- |
| `next` | `static ShaderId next()` | Atomically increments a global counter and returns the new ID. |
| `get`  | `uint32_t get() const`   | Returns the raw numeric value.                                 |

**Notes:**
- There is no public numeric constructor; only `next()` allocates IDs.
- Used as an internal identity key, not exposed through asset handles (asset handles use `assets::AssetId<Shader>`).

---

## 2. ShaderDefVal

```cpp
struct ShaderDefVal {
    std::string name;
    std::variant<bool, int32_t, uint32_t> value;

    // Construct a Bool define (name only → true)
    explicit ShaderDefVal(std::string name);          // Bool(name, true)

    static ShaderDefVal from_bool(std::string name, bool value = true);
    static ShaderDefVal from_int (std::string name, int32_t  value);
    static ShaderDefVal from_uint(std::string name, uint32_t value);

    std::string value_as_string() const;
    bool operator==(const ShaderDefVal&) const = default;
};
template<>
struct std::hash<ShaderDefVal> {
    std::size_t operator()(const ShaderDefVal&) const noexcept;
};
```

**Kind:** Struct (tagged by the active variant of the `value` field).

**Purpose:** Represents a compile-time shader preprocessor `#define` that is inlined into shader source before compilation. The `name` field holds the define name; the `value` variant holds its typed value.

**Value variants:**

| Active type in `value` | Equivalent define         | Description              |
| ---------------------- | ------------------------- | ------------------------ |
| `bool`                 | `#define NAME true/false` | Boolean define.          |
| `int32_t`              | `#define NAME <i>`        | Signed integer define.   |
| `uint32_t`             | `#define NAME <u>`        | Unsigned integer define. |

**`std::hash` specialization** is required so that a sorted `std::vector<ShaderDefVal>` can be used as a cache key.

**Implicit construction from `std::string_view`:** Treated as `from_bool(name, true)` — a bare name without a value creates a boolean `true` define.

**Methods:**

### `ShaderDefVal::value_as_string`

```cpp
std::string ShaderDefVal::value_as_string() const;
```

Returns the string representation of the define's value:
- `bool` → `"true"` or `"false"`
- `int32_t` → decimal string
- `uint32_t` → decimal string

---

## 3. ValidateShader

```cpp
enum class ValidateShader : uint8_t {
    Disabled = 0,  // default
    Enabled  = 1,
};
```

**Kind:** `enum class`.

**Purpose:** Controls whether runtime safety checks (e.g. bounds checking) are injected into the compiled shader module. Trading safety for speed.

**Values:**

| Value                    | Description                                                                                                                                     |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `Disabled` (**default**) | No runtime checks. Suitable for trusted, application-authored shaders.                                                                          |
| `Enabled`                | Runtime bounds/soundness checks enabled. Required for untrusted user-generated shaders. Do **not** use with SPIR-V input (undefined behaviour). |

**Notes:**
- SPIR-V path is undefined/asserts if `Enabled` is used because validation is not implemented for that branch.
- The value is stored per-`Shader` and passed through into `ShaderCache::get` → `load_module`.

---

## 4. Source

```cpp
struct Source {
    struct Wgsl  { std::string code; };
    struct SpirV { std::vector<uint8_t> bytes; };

    std::variant<Wgsl, SpirV> data;

    static Source wgsl (std::string code);
    static Source spirv(std::vector<uint8_t> bytes);

    bool is_wgsl()  const;
    bool is_spirv() const;

    // Returns a string_view into the WGSL text source.
    // Undefined behaviour / assertion failure for SpirV.
    std::string_view as_str() const;
};
```

**Kind:** Struct containing a `std::variant` discriminated union.

**Purpose:** Holds raw, unprocessed shader source code. Only WGSL and SPIR-V are supported.

**Variant types:**

| Inner type | Data                         | Description              |
| ---------- | ---------------------------- | ------------------------ |
| `Wgsl`     | `std::string code`           | WGSL source text.        |
| `SpirV`    | `std::vector<uint8_t> bytes` | Raw SPIR-V binary bytes. |

**Methods:**

### `Source::as_str`

```cpp
std::string_view Source::as_str() const;
```

Returns a `std::string_view` into the WGSL text source.  
**Asserts / undefined behaviour** for `SpirV`.

---

## 5. ShaderImport

```cpp
struct ShaderImport {
    enum class Kind { AssetPath, Custom };
    Kind        kind;
    std::string path;   // the asset path or custom name

    static ShaderImport asset_path(std::string path);
    static ShaderImport custom    (std::string name);

    // Returns the canonical module name used by the composer:
    //   AssetPath(s) -> '"' + s + '"'
    //   Custom(s)    -> s  (verbatim)
    std::string module_name() const;

    bool operator==(const ShaderImport&) const = default;
};
template<>
struct std::hash<ShaderImport> {
    std::size_t operator()(const ShaderImport&) const noexcept;
};
```

**Kind:** Enum.

**Purpose:** Identifies and distinguishes two kinds of shader import references:
- An **asset path** (e.g. `"shaders/my_shader.wgsl"`) that corresponds to a file on disk and is loaded by the asset server.
- A **custom module name** (e.g. `"my_crate::shaders::my_module"`) declared with `#define_import_path` inside WGSL.

**Variants:**

| Variant     | Data     | Description                                                                                       |
| ----------- | -------- | ------------------------------------------------------------------------------------------------- |
| `AssetPath` | `String` | A relative asset path (forward-slash-separated). Used as the key in asset-path import resolution. |
| `Custom`    | `String` | A named import path declared in the WGSL source via `#define_import_path <name>`.                 |

**Methods:**

### `ShaderImport::module_name`

```cpp
std::string ShaderImport::module_name() const;
```

Returns the canonical module name string used internally by the WGSL composer:
- `Kind::AssetPath` → `'"' + path + '"'` (the path wrapped in double quotes).
- `Kind::Custom` → `path` verbatim (the custom name passed through as-is).

---

## 6. Shader

```cpp
struct Shader {
    std::string              path;
    Source                   source;
    ShaderImport             import_path;
    std::vector<ShaderImport> imports;
    std::vector<ShaderDefVal> shader_defs;
    std::vector<assets::Handle<Shader>> file_dependencies;
    ValidateShader           validate_shader = ValidateShader::Disabled;

private:
    static std::pair<ShaderImport, std::vector<ShaderImport>>
        preprocess(std::string_view source, std::string_view path);

public:
    static Shader from_wgsl(std::string source, std::string path);
    static Shader from_wgsl_with_defs(std::string source, std::string path,
                                      std::vector<ShaderDefVal> shader_defs);
    static Shader from_spirv(std::vector<uint8_t> source, std::string path);
};
```

**Kind:** Struct. Registered as an asset type.

**Purpose:** An "unprocessed" shader. Holds raw source code plus metadata needed to resolve its imports, apply preprocessor defines, and track asset dependencies. The `ShaderCache` consumes this to produce a GPU shader module.

**Fields:**

| Field               | Type                                  | Description                                                                                                                                                            |
| ------------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `path`              | `std::string`                         | The asset path of this shader file. Forward-slash-separated. Used as fallback import path when no `#define_import_path` is declared.                                   |
| `source`            | `Source`                              | Raw source code (WGSL / SPIR-V).                                                                                                                                       |
| `import_path`       | `ShaderImport`                        | The path by which other shaders can import this module. If the WGSL source contains `#define_import_path <name>`, this is `Kind::Custom`. Otherwise `Kind::AssetPath`. |
| `imports`           | `std::vector<ShaderImport>`           | All import paths this shader depends on, extracted from preprocessor directives.                                                                                       |
| `shader_defs`       | `std::vector<ShaderDefVal>`           | Shader defines always active when this module is used (merged with caller-supplied defs).                                                                              |
| `file_dependencies` | `std::vector<assets::Handle<Shader>>` | Strong asset handles to dependency shaders, preventing them from being unloaded. Populated by the asset loader.                                                        |
| `validate_shader`   | `ValidateShader`                      | Whether to enable GPU-side runtime validation. Default: `Disabled`.                                                                                                    |

**Private method:**

### `Shader::preprocess` (private)

```cpp
static std::pair<ShaderImport, std::vector<ShaderImport>>
    Shader::preprocess(std::string_view source, std::string_view path);
```

Parses the shader source text to extract:
- The `#define_import_path` declaration (if any) → `ShaderImport::custom(name)`, otherwise `ShaderImport::asset_path(path)`.
- All `#import` / `#import "path"` statements → `std::vector<ShaderImport>`. Quoted imports → `AssetPath`, unquoted → `Custom`.

Returns `{import_path, imports}`.

**Public constructors:**

### `Shader::from_wgsl`

```cpp
static Shader Shader::from_wgsl(std::string source, std::string path);
```

Creates a WGSL shader. Calls `preprocess` to populate `import_path` and `imports`. All other fields are default/empty. `validate_shader` defaults to `Disabled`.

### `Shader::from_wgsl_with_defs`

```cpp
static Shader Shader::from_wgsl_with_defs(
    std::string source,
    std::string path,
    std::vector<ShaderDefVal> shader_defs
);
```

Same as `from_wgsl` but overwrites `shader_defs` with the supplied list.

### `Shader::from_spirv`

```cpp
static Shader Shader::from_spirv(std::vector<uint8_t> source, std::string path);
```

Creates a SPIR-V shader. **Does not call `preprocess`** (no text to parse). `imports` is empty, `import_path` is always `ShaderImport::asset_path(path)`.

**Conversion helpers (for integration with a WGSL composer):**

| Use                               | Notes                                                                                                                                     |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| As a composable module descriptor | Converts `shader_defs` to the composer's map format. Sets module name from `import_path` (quoted for `AssetPath`, verbatim for `Custom`). |
| As a final compilation descriptor | Minimal descriptor used for final compilation, not import registration.                                                                   |

---

## 7. ShaderSettings

```cpp
struct ShaderSettings : assets::Settings {
    std::vector<ShaderDefVal> shader_defs;
};
```

**Kind:** Struct. Asset loader `Settings` type.

**Purpose:** Serializable settings stored in `.meta` files alongside shader assets. Allows specifying extra `#define`s per asset file. Currently only applied when loading `.wgsl` files; other formats emit a warning and ignore defs.

**Fields:**

| Field         | Type                        | Description                                                                |
| ------------- | --------------------------- | -------------------------------------------------------------------------- |
| `shader_defs` | `std::vector<ShaderDefVal>` | Defines to merge into the shader. Passed to `Shader::from_wgsl_with_defs`. |

---

## 8. ShaderLoaderError

```cpp
struct ShaderLoaderError {
    struct Io {
        std::error_code       code;  // OS-level error code
        std::filesystem::path path;  // file that failed to open/read
    };
    struct Parse {
        std::filesystem::path path;         // shader file path
        std::size_t           byte_offset;  // index of the first invalid UTF-8 byte
    };

    std::variant<Io, Parse> data;

    static ShaderLoaderError io   (std::error_code code, std::filesystem::path path);
    static ShaderLoaderError parse(std::filesystem::path path, std::size_t byte_offset);
};
```

**Kind:** Struct. Error type using a `std::variant` discriminated union.

**Inner types:**

| Inner type | Fields                                                | Description                                                                                     |
| ---------- | ----------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `Io`       | `std::error_code code; std::filesystem::path path`    | OS-level IO failure; `code` is the system error, `path` is the file that could not be read.     |
| `Parse`    | `std::filesystem::path path; std::size_t byte_offset` | File bytes could not be decoded as UTF-8; `byte_offset` is the index of the first invalid byte. |

**Notes:** Adding new inner types is non-breaking to callers that use `std::visit` with a generic fallback.

---

## 9. ShaderLoader

```cpp
struct ShaderLoader {
    using Asset    = Shader;
    using Settings = ShaderSettings;
    using Error    = ShaderLoaderError;

    static std::span<const std::string_view> extensions();
    static std::expected<Shader, Error> load(
        std::istream&        reader,
        const Settings&      settings,
        assets::LoadContext& context
    );
};
```

**Kind:** Struct. Implements the engine's `AssetLoader` concept.

**Purpose:** The registered asset loader for raw shader files. Reads file bytes, delegates to the appropriate `Shader::from_*` constructor based on the file extension, and collects asset handles for imported file dependencies.

**Associated types:**

| Type       | Value               |
| ---------- | ------------------- |
| `Asset`    | `Shader`            |
| `Settings` | `ShaderSettings`    |
| `Error`    | `ShaderLoaderError` |

**`extensions` method:**

```cpp
static std::span<const std::string_view> ShaderLoader::extensions();
// Returns: {"spv", "wgsl"}
```

**`load` method:**

```cpp
static std::expected<Shader, ShaderLoaderError> ShaderLoader::load(
    std::istream& reader, const ShaderSettings& settings, assets::LoadContext& context);
```

Logic:
1. Determine extension from `load_context.path()`.
2. Normalize path separators to `/` (Windows workaround).
3. Read all bytes from `reader` via `read_to_end`.
4. Warn if `settings.shader_defs` is non-empty for non-WGSL extensions (defs are silently ignored).
5. Dispatch by extension:
   - `"spv"`  → `Shader::from_spirv(bytes, path)`
   - `"wgsl"` → `Shader::from_wgsl_with_defs(utf8_text, path, settings.shader_defs)`
   - anything else → return `ShaderLoaderError::parse(path, 0)`
6. For each `ShaderImport::AssetPath(asset_path)` in `shader.imports`, call `load_context.load(asset_path)` to obtain a strong handle and push it to `shader.file_dependencies`.
7. Return the `Shader`.

---

## 10. ShaderRef

```cpp
struct ShaderRef {
    struct Default {};
    struct ByHandle { assets::Handle<Shader> handle; };
    struct ByPath   { std::filesystem::path  path;   };

    // Default-constructed value is Default{}
    std::variant<Default, ByHandle, ByPath> value;

    static ShaderRef from_handle(assets::Handle<Shader> handle);
    static ShaderRef from_path  (std::filesystem::path  path);
    static ShaderRef from_str   (std::string_view       path);

    bool is_default() const;
    bool is_handle()  const;
    bool is_path()    const;
};
```

**Kind:** Enum.

**Purpose:** A flexible reference to a shader used in pipeline descriptors. Allows callers to leave the shader selection to some downstream default, supply a strong handle, or supply a path string.

**Variants:**

| Variant                 | Data                     | Description                                                                                    |
| ----------------------- | ------------------------ | ---------------------------------------------------------------------------------------------- |
| `Default` (**default**) | —                        | Use whatever "default" shader the current context implies (e.g. a material's built-in shader). |
| `ByHandle`              | `assets::Handle<Shader>` | Direct reference to an already-loaded shader asset.                                            |
| `ByPath`                | `std::filesystem::path`  | Load/resolve the shader from a path at the time the pipeline is created.                       |

**Factory functions:**

| Function         | Variant produced                   |
| ---------------- | ---------------------------------- |
| `from_handle(h)` | `ByHandle{h}`                      |
| `from_path(p)`   | `ByPath{p}`                        |
| `from_str(s)`    | `ByPath{std::filesystem::path{s}}` |

---

## 11. CachedPipelineId

```cpp
struct CachedPipelineId : utils::int_base<uint64_t> {
    using utils::int_base<uint64_t>::int_base;
};
template<>
struct std::hash<CachedPipelineId> {
    std::size_t operator()(const CachedPipelineId& id) const noexcept {
        return std::hash<uint64_t>{}(id.get());
    }
};
```

**Kind:** Strongly-typed newtype over `uint64_t` (using `utils::int_base`).

**Purpose:** An opaque numeric identifier for a pipeline entry in the `PipelineCache`. Used by `ShaderCache` solely as a dependency-tracking key — when a shader is invalidated, all `CachedPipelineId`s that used it are returned so the pipeline cache can re-queue them.

**Notes:**
- No implicit construction from integers; callers must use the typed newtype explicitly.
- `std::hash` specialization is required for use as an `std::unordered_set` key in `ShaderData`.

---

## 12. ShaderData (internal)

```cpp
struct ShaderData {
    std::unordered_set<CachedPipelineId>                                                pipelines;
    std::unordered_map<std::vector<ShaderDefVal>, std::shared_ptr<wgpu::ShaderModule>>  processed_shaders;
    std::unordered_map<ShaderImport, assets::AssetId<Shader>>                           resolved_imports;
    std::unordered_set<assets::AssetId<Shader>>                                         dependents;
};
```

**Kind:** Struct. Private / internal to `ShaderCache`. Not exported.

**Purpose:** Per-shader bookkeeping inside `ShaderCache`. Stores everything the cache needs to:
- Find a cached compiled module for a given set of shader defs.
- Know which pipelines depend on this shader (for invalidation).
- Know which imports have been resolved (to gate compilation until all imports are ready).
- Know which other shaders import this one (transitive invalidation).

**Fields:**

| Field               | Type                                                                                 | Description                                                                                                                                          |
| ------------------- | ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `pipelines`         | `std::unordered_set<CachedPipelineId>`                                               | All pipeline IDs that requested a module from this shader. Used to return affected pipelines on invalidation.                                        |
| `processed_shaders` | `std::unordered_map<std::vector<ShaderDefVal>, std::shared_ptr<wgpu::ShaderModule>>` | Cache keyed by the **exact, ordered** list of defs. Each unique def combination gets its own compiled module.                                        |
| `resolved_imports`  | `std::unordered_map<ShaderImport, assets::AssetId<Shader>>`                          | Maps this shader's declared imports → their resolved asset IDs. When `AssetPath` import count equals resolved count, the shader is ready to compile. |
| `dependents`        | `std::unordered_set<assets::AssetId<Shader>>`                                        | Other shaders that import this one. When this shader changes, all dependents must also be invalidated.                                               |

**Default construction:** All fields default to empty collections.

---

## 13. ShaderCacheSource

```cpp
struct ShaderCacheSource {
    // Zero-copy borrow of the SPIR-V bytes from Shader::source.
    struct SpirV { std::span<const uint8_t> bytes; };
    // Fully composed WGSL text, owned.
    struct Wgsl  { std::string source; };

    std::variant<SpirV, Wgsl> data;
};
```

**Kind:** Struct containing a discriminated union.

**Purpose:** The fully composed, def-resolved shader source ready to be handed to `load_module` (the renderer-supplied compilation function). Equivalent to `wgpu::ShaderSource` but lighter-weight; naga IR is NOT carried here since C++ does not link naga directly.

**Variant types:**

| Inner type | Data                             | Description                                                                                     |
| ---------- | -------------------------------- | ----------------------------------------------------------------------------------------------- |
| `SpirV`    | `std::span<const uint8_t> bytes` | Raw SPIR-V word bytes, borrowed from the original `Source::SpirV`. Caller must ensure lifetime. |
| `Wgsl`     | `std::string source`             | Fully composed WGSL text (after import/def resolution).                                         |

**Notes:**
- The renderer receives this value and is responsible for any further translation (e.g. WGSL → SPIR-V for Vulkan).
- In the C++ engine, a `Naga` variant is omitted; the composer always produces WGSL text output.

---

## 14. ShaderCacheError

> **Note:** `ComposeError` referenced by `ProcessShaderError` is defined in §16 (ShaderComposer).

```cpp
struct ShaderCacheError {
    struct ShaderNotLoaded             { assets::AssetId<Shader> id; };
    struct ProcessShaderError          { ComposeError error; };
    struct ShaderImportNotYetAvailable {};
    struct CreateShaderModule          { std::string wgpu_message; };

    std::variant<ShaderNotLoaded, ProcessShaderError,
                 ShaderImportNotYetAvailable, CreateShaderModule> data;

    static ShaderCacheError not_loaded(assets::AssetId<Shader> id);
    static ShaderCacheError process_error(ComposeError error);
    static ShaderCacheError import_not_available();
    static ShaderCacheError create_module_failed(std::string wgpu_message);
};
```

**Kind:** Struct. Error type using a `std::variant` discriminated union.

**Inner types:**

| Inner type                    | Fields                       | Description                                                                                          |
| ----------------------------- | ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| `ShaderNotLoaded`             | `assets::AssetId<Shader> id` | The shader asset with this ID has not been inserted into the cache yet.                              |
| `ProcessShaderError`          | `ComposeError error`         | The composer failed to process the module (unknown import, WGSL parse error, etc.).                  |
| `ShaderImportNotYetAvailable` | —                            | At least one of the shader's `AssetPath` imports has not yet been loaded. Caller should retry later. |
| `CreateShaderModule`          | `std::string wgpu_message`   | `load_module` (wgpu device) reported an error; `wgpu_message` is the device error string.            |

---

## 15. ShaderCache

```cpp
struct ShaderCache {
    using LoadModuleFn = std::function<
        std::expected<wgpu::ShaderModule, ShaderCacheError>(
            const wgpu::Device&,
            const ShaderCacheSource&,
            ValidateShader)>;
private:
    wgpu::Device                                                             device;
    std::unordered_map<assets::AssetId<Shader>, ShaderData>                 data;
    LoadModuleFn                                                             load_module;
    std::unordered_map<assets::AssetId<Shader>, Shader>                     shaders;
    std::unordered_map<ShaderImport, assets::AssetId<Shader>>               import_path_shaders;
    std::unordered_map<ShaderImport, std::vector<assets::AssetId<Shader>>>  waiting_on_import;
    ShaderComposer                                                           composer;

public:
    ShaderCache(wgpu::Device device, LoadModuleFn load_module);

    std::expected<std::shared_ptr<wgpu::ShaderModule>, ShaderCacheError>
        get(CachedPipelineId pipeline,
            assets::AssetId<Shader> id,
            std::span<const ShaderDefVal> shader_defs);

    std::vector<CachedPipelineId> set_shader(assets::AssetId<Shader> id, Shader shader);
    std::vector<CachedPipelineId> remove    (assets::AssetId<Shader> id);

private:
    std::vector<CachedPipelineId> clear(assets::AssetId<Shader> id);

    static std::expected<void, ShaderCacheError> add_import_to_composer(
        ShaderComposer& composer,
        const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_path_shaders,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
        const ShaderImport& import
    );
};
```

**Kind:** Struct. Uses `wgpu::ShaderModule` and `wgpu::Device` as concrete types; `ShaderComposer` handles WGSL import composition.

**Purpose:** The central shader compilation and caching subsystem. Manages the full lifecycle of shader assets:
- Stores raw `Shader` assets indexed by `assets::AssetId`.
- Composes shaders (resolves imports, applies defs) via a WGSL composer.
- Caches compiled `ShaderModule` objects keyed by `(AssetId, [ShaderDefVal])`.
- Tracks import dependencies and defers compilation until all imports are available.
- Propagates invalidation to dependent shaders and dependent pipelines when a shader changes.

**Fields (all private except `composer`):**

| Field                 | Type                                                                     | Description                                                                                          |
| --------------------- | ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------- |
| `device`              | `wgpu::Device`                                                           | The WebGPU device, passed to `load_module`.                                                          |
| `data`                | `std::unordered_map<assets::AssetId<Shader>, ShaderData>`                | Per-shader bookkeeping (see `ShaderData`).                                                           |
| `load_module`         | `LoadModuleFn`                                                           | Callback: takes device, composed source, validation flag → compiled `wgpu::ShaderModule` or error.   |
| `shaders`             | `std::unordered_map<assets::AssetId<Shader>, Shader>`                    | All currently loaded `Shader` assets.                                                                |
| `import_path_shaders` | `std::unordered_map<ShaderImport, assets::AssetId<Shader>>`              | Maps import paths to the asset that provides them, enabling import resolution.                       |
| `waiting_on_import`   | `std::unordered_map<ShaderImport, std::vector<assets::AssetId<Shader>>>` | Maps an unresolved import path to the list of shaders waiting for it to load.                        |
| `composer`            | `ShaderComposer`                                                         | The WGSL import composer (see §16). Holds all registered modules and performs import+def resolution. |

### `ShaderCache` constructor

```cpp
ShaderCache(
    wgpu::Device device,
    LoadModuleFn load_module
);
```

- Stores `device` and `load_module`.
- Constructs a default `ShaderComposer` (see §16).
- All collections start empty.

**Private methods:**

### `ShaderCache::add_import_to_composer` (private, static)

```cpp
static std::expected<void, ShaderCacheError>
    ShaderCache::add_import_to_composer(
        ShaderComposer& composer,
        const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_path_shaders,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
        const ShaderImport& import
    );
```

Recursively registers a shader module and all of its transitive imports with the WGSL composer:
1. Early return if `composer.contains_module(import.module_name())` — already registered.
2. Look up the shader via `import_path_shaders` then `shaders`. Return `ShaderImportNotYetAvailable` if not found.
3. Recurse for each of the found shader's own `imports`.
4. Call `composer.add_module(module_name, source, shader.shader_defs)`. Map `ComposeError` to `ShaderCacheError::ProcessShaderError`.

### `ShaderCache::clear` (private)

```cpp
std::vector<CachedPipelineId> ShaderCache::clear(assets::AssetId<Shader> id);
```

Breadth-first invalidation starting from `id`:
1. Collect `id` into a work queue.
2. For each shader in the queue:
   - Clear `data[id].processed_shaders` (drop compiled modules, releasing `std::shared_ptr` ownership).
   - Collect affected pipelines from `data[id].pipelines`.
   - Push all `data[id].dependents` into the work queue (transitive invalidation).
   - Remove the shader's module from the composer.
3. Return all collected `CachedPipelineId`s.

**Public methods:**

### `ShaderCache::get`

```cpp
std::expected<std::shared_ptr<ShaderModule>, ShaderCacheError>
    ShaderCache::get(
        CachedPipelineId pipeline,
        assets::AssetId<Shader> id,
        std::span<const ShaderDefVal> shader_defs
    );
```

Retrieves or compiles the module for `(id, shader_defs)`:

1. Look up `Shader` by `id` → `ShaderNotLoaded` if absent.
2. Count `AssetPath`-type imports and compare against `data[id].resolved_imports` count. If they differ, return `ShaderImportNotYetAvailable` (some file imports are not yet ready).
3. Insert `pipeline` into `data[id].pipelines` (dependency tracking).
4. Check `data[id].processed_shaders` for an existing entry with matching defs:
   - **Cache hit:** Return the cached `std::shared_ptr<ShaderModule>`.
   - **Cache miss:** Compose and compile:
     - `Source::SpirV` → `ShaderCacheSource::SpirV{bytes}`
     - `Source::Wgsl` → Call `add_import_to_composer` for every import, merge `shader_defs` with `shader.shader_defs`, call `composer.compose(source, path, merged_defs)` → `ShaderCacheSource::Wgsl{wgsl_text}`.
     - Call `load_module(device, shader_source, shader.validate_shader)`.
     - Wrap result in `std::shared_ptr` and insert into `processed_shaders`.
5. Return a copy of the `std::shared_ptr<ShaderModule>`.

**Note on cache key:** The key is the `std::vector<ShaderDefVal>` of defs. Order-sensitive: `[A, B]` ≠ `[B, A]`. Callers must sort or normalise defs for consistent cache hits.

### `ShaderCache::set_shader`

```cpp
std::vector<CachedPipelineId>
    ShaderCache::set_shader(assets::AssetId<Shader> id, Shader shader);
```

Inserts or replaces a shader, returning all affected pipeline IDs:

1. Call `clear(id)` to invalidate any existing compiled modules and collect stale pipeline IDs.
2. Register `shader.import_path → id` in `import_path_shaders`.
3. For shaders that were in `waiting_on_import` for this shader's import path: resolve their import in `data[waiting].resolved_imports`, and add them as `dependents` of this shader.
4. For each of this shader's own imports:
   - If already in `import_path_shaders`: resolve immediately (store in `resolved_imports`, add reverse dependency).
   - Else: add to `waiting_on_import[import]`.
5. Insert `shader` into `shaders`.
6. Return collected pipeline IDs.

### `ShaderCache::remove`

```cpp
std::vector<CachedPipelineId> ShaderCache::remove(assets::AssetId<Shader> id);
```

Removes a shader from the cache:

1. Call `clear(id)` to invalidate compiled modules and collect pipeline IDs.
2. Remove from `shaders` and remove the `import_path → id` mapping from `import_path_shaders`.
3. Return pipeline IDs.

**Note:** Does not remove entries from `waiting_on_import` for this shader's imports. Those dangling waiting entries are harmless and will be resolved correctly if the shader is re-inserted later.

## 16. ComposeError and ShaderComposer

### ComposeError

```cpp
struct ComposeError {
    struct ImportNotFound {
        std::string import_name;  // the unresolved #import path
    };
    struct ParseError {
        std::string module_name;
        std::string details;      // human-readable description of the parse failure
    };
    struct CircularImport {
        std::vector<std::string> cycle_chain;  // import names forming the cycle, e.g. ["A","B","A"]
    };
    std::variant<ImportNotFound, ParseError, CircularImport> data;
};
```

**Kind:** Struct. Error type using a `std::variant` discriminated union. Emitted by `ShaderComposer`.

**Inner types:**

| Inner type       | Fields                                         | Description                                                                                 |
| ---------------- | ---------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `ImportNotFound` | `std::string import_name`                      | A `#import` directive names a module not registered with the composer.                      |
| `ParseError`     | `std::string module_name; std::string details` | The WGSL source of a module could not be parsed; `details` carries the error description.   |
| `CircularImport` | `std::vector<std::string> cycle_chain`         | The import graph contains a cycle; `cycle_chain` lists the path of module names forming it. |

---

### ShaderComposer

```cpp
struct ShaderComposer {
    // Add a composable module (a shader that can be #imported by others).
    // `module_name` is the string used in #import directives.
    // `defs` are applied while parsing to pre-strip inactive #ifdef branches.
    // Returns ComposeError if parsing fails or a circular import is detected.
    std::expected<void, ComposeError>
        add_module(const std::string&            module_name,
                   std::string_view              source,
                   std::span<const ShaderDefVal> defs);

    // Remove a previously registered module by name. Silently ignores unknown names.
    void remove_module(const std::string& module_name);

    // Returns true if a module with this name is already registered.
    bool contains_module(const std::string& module_name) const;

    // Resolve all #import directives in `source` and apply `additional_defs`
    // (#ifdef / #if guards), returning a flat WGSL string ready for
    // wgpu::Device::createShaderModule.
    // Returns ComposeError if any import is unresolved or a parse error occurs.
    std::expected<std::string, ComposeError>
        compose(std::string_view              source,
                std::string_view              file_path,
                std::span<const ShaderDefVal> additional_defs);
};
```

**Kind:** Struct. The engine's own WGSL import-composition engine. No external library; custom component of `epix_engine`.

**Purpose:** Pre-processes WGSL shader source by:
1. Resolving `#import` directives (substituting registered composable modules inline).
2. Applying shader defines from `ShaderDefVal` lists (`#ifdef NAME`, `#if NAME == value`, etc.).
3. Returning a flat, self-contained WGSL string ready for `wgpu::Device::createShaderModule`.

**Methods:**

### `ShaderComposer::add_module`

```cpp
std::expected<void, ComposeError>
    ShaderComposer::add_module(
        const std::string&            module_name,
        std::string_view              source,
        std::span<const ShaderDefVal> defs
    );
```

Parses `source` as WGSL and registers it under `module_name`. `defs` are applied during parsing to remove inactive `#ifdef` branches in the stored representation. Returns `ComposeError::ParseError` on parse failure.

### `ShaderComposer::remove_module`

```cpp
void ShaderComposer::remove_module(const std::string& module_name);
```

Unregisters the named module. Silently does nothing if the name is not found. Called from `ShaderCache::clear` during shader invalidation.

### `ShaderComposer::contains_module`

```cpp
bool ShaderComposer::contains_module(const std::string& module_name) const;
```

Returns `true` if a module with the given name is currently registered. Used by `ShaderCache::add_import_to_composer` to skip re-registration.

### `ShaderComposer::compose`

```cpp
std::expected<std::string, ComposeError>
    ShaderComposer::compose(
        std::string_view              source,
        std::string_view              file_path,
        std::span<const ShaderDefVal> additional_defs
    );
```

Fully composes `source`:
1. Applies `additional_defs` to resolve `#ifdef` / `#if` guards.
2. For each `#import` directive, inlines the registered module's (already composed) body.
3. Returns the resulting flat WGSL string.

Returns `ComposeError::ImportNotFound` if any `#import` names an unregistered module, or `ComposeError::CircularImport` if a dependency cycle is detected.

---

## 17. EPIX_LOAD_SHADER_LIBRARY (macro)

```cpp
// Registers shader source bytes as an embedded asset and permanently loads it,
// keeping the handle alive for the lifetime of the application.
//
// Usage:
//   EPIX_LOAD_SHADER_LIBRARY(app, "shaders/my_shader.wgsl");
//   EPIX_LOAD_SHADER_LIBRARY(app, "shaders/my_shader.wgsl", settings);
#define EPIX_LOAD_SHADER_LIBRARY(app, path, ...)                              \
    do {                                                                       \
        epix::assets::embedded_asset((app), (path));                           \
        auto _handle = epix::assets::load_embedded_asset<Shader>(             \
            (app), (path) __VA_OPT__(, __VA_ARGS__));                          \
        /* Intentionally leak the handle to keep the shader loaded forever */  \
        static auto _permanent_handle = std::move(_handle);                   \
    } while(0)
```

**Kind:** Preprocessor macro.

**Purpose:** Inline-embeds a shader file as an embedded asset (bytes baked into the binary) and immediately loads it, keeping the handle alive permanently. Works around a limitation where the shader loader does not correctly load transitive dependencies of shaders that are only referenced (not directly loaded) early in initialization.

**Parameters:**

| Parameter | Description                                                           |
| --------- | --------------------------------------------------------------------- |
| `app`     | An expression yielding a reference to the application/asset-server.   |
| `path`    | A string literal path to the shader file, relative to the asset root. |
| `...`     | Optional `ShaderSettings` to pass to the loader.                      |

**Expansion steps:**
1. `epix::assets::embedded_asset(app, path)` — registers the raw bytes of the file at `path` as an embedded asset baked into the binary.
2. `epix::assets::load_embedded_asset<Shader>(app, path, [settings])` — loads the embedded asset, returning a `Handle<Shader>`.
3. Store the handle in a `static` local variable so it is never destroyed — the shader stays loaded for the entire application lifetime.

---

*End of reference document.*

---

