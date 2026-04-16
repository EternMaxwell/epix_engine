# Shader Cache

Runtime cache that resolves shader imports, composes WGSL or compiles Slang, and vends
compiled `wgpu::ShaderModule` variants keyed by definition set.

## `ShaderCache`

### Overview

`ShaderCache` is a resource inserted into the app by pipeline systems. It sits between the
asset server (which loads raw `Shader` assets) and the GPU (which needs a compiled
`wgpu::ShaderModule`). Its responsibilities are:

- Tracking every `Shader` that has been loaded or changed.
- Resolving recursive `ShaderImport` chains.
- Composing final WGSL (via `ShaderComposer`) or invoking Slang to compile to WGSL.
- Calling the user-supplied `LoadModuleFn` to create a `wgpu::ShaderModule` from the
  final WGSL or SPIR-V.
- Caching compiled modules per `(shader_id, shader_defs)` pair.
- Reporting which `CachedPipelineId`s need to rebuild when a shader changes.

### Usage

```cpp
// Construct with a device and a backend loader callback.
ShaderCache cache{
    device,
    [](const wgpu::Device& dev, const ShaderCacheSource& src, ValidateShader validate)
        -> std::expected<wgpu::ShaderModule, ShaderCacheError>
    {
        if (auto* wgsl = std::get_if<ShaderCacheSource::Wgsl>(&src.data)) {
            wgpu::ShaderModuleDescriptor desc{};
            wgpu::ShaderModuleWGSLDescriptor wgsl_desc{};
            wgsl_desc.code = wgsl->source.c_str();
            // ... fill descriptor, call dev.createShaderModule(desc)
        }
        return wgpu::ShaderModule{};
    }
};

// Insert as a resource so systems can access it.
app.world_mut().insert_resource(std::move(cache));
```

**Typical per-frame flow:**

```cpp
// In an Update or PostUpdate system:
auto& assets = app.resource<Assets<Shader>>();
auto& events = app.resource<Events<AssetEvent<Shader>>>();
auto& cache  = app.resource_mut<ShaderCache>();

// 1. Sync: ingest asset events, returns pipelines that need to rebuild.
auto dirty = cache.sync(events.drain(), assets);
for (auto pid : dirty) { rebuild_pipeline(pid); }

// 2. Get: retrieve (or build) a compiled shader variant for a pipeline.
const std::array extra_defs = {ShaderDefVal::from_bool("USE_SHADOWS")};
auto result = cache.get(pipeline_id, shader_handle.id(), extra_defs);
if (result.has_value()) {
    use_module(*result);
} else if (!result.error().is_recoverable()) {
    spdlog::error("shader error: {}", result.error().message());
}
```

### Methods

| Method       | Signature                           | Description                                                                        |
| ------------ | ----------------------------------- | ---------------------------------------------------------------------------------- |
| Constructor  | `ShaderCache(device, LoadModuleFn)` | Bind the cache to one device and one backend module creator.                       |
| `get`        | `get(pipeline, id, shader_defs)`    | Retrieve or build a compiled variant. Returns the module or a `ShaderCacheError`.  |
| `set_shader` | `set_shader(id, shader)`            | Insert or replace one shader; returns affected pipeline IDs.                       |
| `remove`     | `remove(id)`                        | Remove a shader; returns affected pipeline IDs.                                    |
| `sync`       | `sync(events, shaders)`             | Process a batch of `AssetEvent<Shader>` events; returns all affected pipeline IDs. |

### `LoadModuleFn`

The callback type stored in `ShaderCache`:

```cpp
using LoadModuleFn = std::function<
    std::expected<wgpu::ShaderModule, ShaderCacheError>(
        const wgpu::Device&,
        const ShaderCacheSource&,
        ValidateShader
    )
>;
```

The callback receives either a `ShaderCacheSource::Wgsl` (final composed WGSL text) or a
`ShaderCacheSource::SpirV` (SPIR-V bytes) and must return a valid `wgpu::ShaderModule` or
a `ShaderCacheError`.

---

## `ShaderCacheError`

### Overview

Error returned by `ShaderCache::get()`. Some errors are *recoverable*: a missing import
may become available once another shader finishes loading.

| Variant                       | Recoverable | Meaning                                                                 |
| ----------------------------- | ----------- | ----------------------------------------------------------------------- |
| `ShaderNotLoaded`             | yes         | The requested `AssetId<Shader>` is not in the cache yet.                |
| `ShaderImportNotYetAvailable` | yes         | One or more imports are not resolved yet. `missing_imports` lists them. |
| `ProcessShaderError`          | no          | WGSL composition failed.                                                |
| `CreateShaderModule`          | no          | Backend module creation failed.                                         |
| `SlangCompileError`           | no          | Slang compilation failed; `stage` and `message` identify where.         |

### Usage

```cpp
auto result = cache.get(pipeline_id, shader_id, defs);
if (!result) {
    const auto& err = result.error();
    if (err.is_recoverable()) {
        // Try again next frame.
    } else {
        spdlog::error("{}", err.message());
    }
}
```

### Factory methods

```cpp
ShaderCacheError::not_loaded(id)
ShaderCacheError::import_not_available({{missing_import_1, missing_import_2}})
ShaderCacheError::process_error(compose_error)
ShaderCacheError::create_module_failed("WGSL validation failed")
ShaderCacheError::slang_error(SlangCacheError::Stage::Compose, "undeclared identifier")
```

---

## `ShaderCacheSource`

### Overview

The processed source handed to `LoadModuleFn`. Only two forms reach the callback:

| Variant                    | Content                                             |
| -------------------------- | --------------------------------------------------- |
| `ShaderCacheSource::Wgsl`  | `std::string_view source` — final composed WGSL.            |
| `ShaderCacheSource::SpirV` | `std::span<const uint8_t> bytes` — SPIR-V bytecode.         |

---

## `CachedPipelineId`

### Overview

Strongly-typed `uint64_t` identifier for a pipeline tracked by `ShaderCache`. Each logical
pipeline owns a unique ID so the cache can report exactly which pipelines are invalidated
when a shader changes.

```cpp
CachedPipelineId id{42};
auto result = cache.get(id, shader_id, defs);
```

`CachedPipelineId` values are assigned and owned by the caller (e.g. `PipelineServer` in
the render module).

---

## `ShaderData`

### Overview

Per-shader tracking state stored inside `ShaderCache`. Exported but primarily an
implementation detail.

| Field               | Type                                                        | Meaning                                     |
| ------------------- | ----------------------------------------------------------- | ------------------------------------------- |
| `pipelines`         | `unordered_set<CachedPipelineId>`                           | Pipelines currently using this shader.      |
| `processed_shaders` | `map<vector<ShaderDefVal>, shared_ptr<wgpu::ShaderModule>>` | Compiled variants cached by definition set. |
| `resolved_imports`  | `map<ShaderImport, AssetId<Shader>>`                        | Import names already matched to asset IDs.  |
| `dependents`        | `unordered_set<AssetId<Shader>>`                            | Shaders that import this one.               |

The `ShaderCache` manages `ShaderData` internally. Direct mutation outside `ShaderCache`
methods is not supported.
