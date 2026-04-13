# EPIX ENGINE SHADER MODULE

Manages shader assets — WGSL, Slang text, SPIR-V, and pre-compiled Slang IR blobs — from
path-based loading through import resolution, WGSL composition, and runtime cached compilation.

## Core Parts

- **[`ShaderPlugin`](./shader-asset.md)**: App plugin; registers shader asset loading and processing into the app.
- **[`Shader`](./shader-asset.md)**: Parsed shader asset. Holds source, declared imports, default definitions, and the import path.
- **[`Source`](./shader-asset.md)**: Source payload variant — `Wgsl`, `SpirV`, `Slang`, or `SlangIr`.
- **[`ShaderDefVal`](./shader-defs.md)**: A named boolean/integer definition used for conditional compilation.
- **[`ValidateShader`](./shader-defs.md)**: Enum toggling backend shader validation on a per-shader basis.
- **[`ShaderImport`](./shader-import.md)**: An import target — either a concrete file path or a custom module name.
- **[`ShaderRef`](./shader-import.md)**: Flexible shader reference: default shader, loaded handle, or filesystem path.
- **[`ShaderLoader`](./shader-loading.md)**: Asset loader; reads `.wgsl`, `.slang`, and `.slang-module` files into `Shader` assets.
- **[`ShaderProcessor`](./shader-loading.md)**: Asset processor; pre-processes shader sources before loading (optional Slang-to-IR compilation).
- **[`ShaderComposer`](./shader-composer.md)**: WGSL-only `#import` expander and `#ifdef`/`#ifndef` conditional evaluator.
- **[`ShaderCache`](./shader-cache.md)**: Runtime cache; resolves imports, composes WGSL or compiles Slang, caches compiled `wgpu::ShaderModule` variants keyed by definition set.
- **[`ShaderCacheError`](./shader-cache.md)**: Error type returned by `ShaderCache`; distinguishes recoverable errors (missing imports) from hard failures.

## Quick Guide

```cpp
import epix.core;
import epix.assets;
import epix.shader;

using namespace epix::core;
using namespace epix::assets;
using namespace epix::shader;

// 1. Build the app with asset and shader support.
App app = App::create();
AssetPlugin{}.build(app);
ShaderPlugin{}.build(app);

// 2. Insert a ShaderCache resource with a backend loader callback.
app.world_mut().insert_resource(ShaderCache{
    device,
    [](const wgpu::Device& dev, const ShaderCacheSource& src, ValidateShader) {
        // Build and return a wgpu::ShaderModule from src (Wgsl or SpirV).
        return wgpu::ShaderModule{};
    }
});

app.run_schedule(Startup);

// 3. Load a shader through the asset server.
auto& server = app.resource<AssetServer>();
auto handle  = server.load<Shader>(AssetPath("shaders/main.wgsl"));

// 4. Each frame: sync asset events into the cache, then retrieve compiled variants.
auto& assets = app.resource<Assets<Shader>>();
auto& cache  = app.resource_mut<ShaderCache>();

// Sync new/modified/removed shaders (returns pipelines that need rebuild).
auto& events = app.resource<Events<AssetEvent<Shader>>>();
cache.sync(events.drain(), assets);

// Retrieve a compiled module for pipeline_id.
auto result = cache.get(CachedPipelineId{pipeline_id}, handle.id(), /*shader_defs=*/{});
if (result.has_value()) {
    wgpu::ShaderModule module = *result;
    // pass module to pipeline creation
} else if (!result.error().is_recoverable()) {
    // hard failure — log result.error().message()
}
```

See [`ShaderPlugin`](./shader-asset.md) for embedding shaders without the file system, and
[`ShaderComposer`](./shader-composer.md) for standalone WGSL `#import` use.
