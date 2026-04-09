---
name: slang-shader-authoring
description: 'Write, register, and debug Slang shaders in epix_engine. Use when authoring .slang shader files, wiring them into the ShaderCache/ShaderLoader pipeline, adding preprocessor defs variants, resolving import errors, or diagnosing Slang compile failures. Covers entry points, module declarations, VFS import rules, and C++ integration API.'
argument-hint: '[shader feature to implement or error to diagnose]'
---

# Slang Shader Authoring

## What This Produces

A working Slang shader file + the C++ wiring to load, compile, and use it at runtime through `epix_shader`'s `ShaderCache`.

---

## Slang Language Rules (Epix-Specific)

### Module Declaration

```slang
// CORRECT — simple identifier
module scene;

// CORRECT — string-literal for dotted/path names
module "epix/view";   // use this for any name containing /  or .

// WRONG — dots NOT allowed in bare identifier form
module epix.view;     // ❌ syntax error
```

### Visibility

All exported symbols **must** be marked `public` — Slang defaults to `internal`:

```slang
public struct Vertex {          // ✅
    public float3 position;     // ✅ members too
    float2 uv;                  // ❌ internal — invisible to importers
}
```

### Entry Points

Use `[shader("...")]` attribute. Entry point names are auto-discovered:

```slang
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) { }

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) { }

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target { }
```

> Epix sets `VulkanUseEntryPointName = true` — entry point names are **preserved** in SPIR-V output. Name them consistently so C++ pipeline creation can reference them by string.

### Instance ID

`SV_InstanceID` requires `DrawParameters` SPIR-V capability (not supported in WebGPU).  
Use `SV_VulkanInstanceID` instead:

```slang
// WRONG
uint instanceID : SV_InstanceID     // ❌ unsupported capability
// CORRECT
uint instanceID : SV_VulkanInstanceID  // ✅
```

### Matrix Layout

Matrices are **column-major** (Vulkan convention). Pass data accordingly from C++.

---

## Import Patterns

| Style | Example | Resolution |
|-------|---------|-----------|
| Custom module name | `import utility;` | Registered in `ShaderCache` as a custom import |
| Root-relative file | `import "/shared/view.slang";` | Loaded as `AssetPath` from root of same source as this shader |
| Full-path file | `import "source://shaders/lighting.slang";` | Loaded as `AssetPath` from root of given source |
| Relative file | `import "common/math.slang";` | Relative to the importing shader's directory |
| `__include` | `__include "defs.slang";` | Textual include; included file must declare `implementing <module>;`, the path search follows the same rules as `import` |

> **Bare identifier import** (`import foo;`) → Slang resolves to `foo.slang` via the VFS. Registration in cache as a `ShaderImport::custom("foo")` entry is required.

---

## C++ Integration API

### 1 — Create a Shader object

```cpp
#import epix.shader;

// Inline source (tests / quick prototypes)
auto shader = Shader::from_slang(source_string, "my_shader.slang");

// With compile-time defs baked into the shader
auto shader = Shader::from_slang_with_defs(source, "my_shader.slang", {
    ShaderDefVal::from_int("BLOCK_SIZE", 8),
    ShaderDefVal::from_bool("ENABLE_SHADOWS", true),
});

// From asset server (preferred for files on disk)
Handle<Shader> handle = asset_server.load<Shader>("shaders/my_shader.slang");
// Or with given asset source
Handle<Shader> handle = asset_server.load<Shader>("source://shaders/my_shader.slang");
```

### 2 — Register in ShaderCache

```cpp
assets::AssetId<Shader> id = ...;  // from asset load or custom ID for inline shaders
cache.set_shader(id, std::move(shader));
```

### 3 — Compile at runtime

```cpp
// No extra defs — uses shader's baked defs only
auto result = cache.get(pipeline_id, id, {});

// With caller defs — caller defs OVERRIDE shader defs, trigger a separate cached variant
auto result = cache.get(pipeline_id, id, {
    ShaderDefVal::from_int("BLOCK_SIZE", 4),
});

if (result) {
    wgpu::ShaderModule module = result.value();  // use in pipeline descriptor
} else {
    auto& err = result.error();
    if (err.is_recoverable()) {
        // Missing import not yet loaded — retry next frame
    } else {
        // Hard compile error — log err.message()
    }
}
```

---

## Asset Pipeline Workflow (Files on Disk)

1. Place `.slang` file under `assets/shaders/`.
2. Load it via `AssetServer::load<Shader>(AssetPath("shaders/my_shader.slang"))`.
3. The `ShaderLoader` parses `import` statements and automatically queues file-based dependencies as additional `Handle<Shader>` loads.
4. Custom module imports (bare identifier style) must be pre-registered in the cache before `cache.get()` is called.
5. Slang source is **not pre-compiled** during asset preprocessing — it compiles on the first `cache.get()` call. Subsequent calls with the same def set return the cached `wgpu::ShaderModule`.

---

## Procedure: Authoring a New Shader

### Step 1 — Write the `.slang` file

```slang
// assets/shaders/my_pass.slang
module "shaders/my_pass";

import "/shared/common.slang";   // file dep, loaded by ShaderLoader

public struct PushConstants {
    public float4x4 transform;
}

ParameterBlock<PushConstants> pc;

[shader("vertex")]
float4 vertexMain(float3 pos : POSITION) : SV_Position {
    return mul(pc.transform, float4(pos, 1.0));
}

[shader("fragment")]
float4 fragmentMain() : SV_Target {
    return float4(1, 0, 0, 1);
}
```

### Step 2 — Load and register

```cpp
// In a startup system:
void setup_shaders(Res<AssetServer> server, ResMut<ShaderCache> cache) {
    auto handle = server->load<Shader>(AssetPath("shaders/my_pass.slang"));
    // Store handle in a resource so it stays alive
    // Cache wiring happens automatically via ShaderLoader on AssetLoadedEvent
}
```

### Step 3 — Compile and use

```cpp
// In a render system, after shader is loaded:
void render(ResMut<ShaderCache> cache, Res<MyShaderIds> ids) {
    auto result = cache.get(pipeline_id, ids->my_pass, {});
    if (!result) { return; }  // not ready yet
    auto& module = result.value();
    // Use module in wgpu::RenderPipelineDescriptor
}
```

---

## Plugin-Managed Automation

When using `epix_shader` and `epix_render` plugins together, most of the manual wiring described above is **handled automatically**. Do not duplicate it.

### ShaderPlugin (`epix::shader::ShaderPlugin`)

Added automatically when the render plugin is used. It:

- Registers `Shader` as an asset type with `ShaderLoader` (handles `.wgsl` and `.slang` files) and `ShaderProcessor`.
- Registers a **`sync shader cache`** system in the `core::Last` schedule that calls `cache->sync(events, shaders)` after every `AssetEvent<Shader>`. This populates `ShaderCache` automatically when shaders finish loading — **you do not call `cache.set_shader()` manually**.

### PipelineServer (`epix::render::PipelineServer`)

Inserted as a resource into the render world. It:

- Runs **`extract_shaders`** every `ExtractSchedule` tick — extracts `Assets<Shader>` and shader events from the main world, syncs them, and re-queues any pipeline whose shader changed.
- Runs **`process_pipeline_system`** every `RenderSet::Render` tick — advances pending pipelines from `Queued` through async thread-pool compilation to `Pipeline` or `Error` state.
- Automatically **re-queues** affected pipelines when any of their shaders are updated via `AssetEvent`.

### What you still do manually

```cpp
// 1. Load shaders through AssetServer (plugin auto-registers them into ShaderCache)
Handle<Shader> h = server->load<Shader>(AssetPath("shaders/my_pass.slang"));

// 2. Queue a pipeline descriptor once — PipelineServer compiles it async
CachedPipelineId id = pipeline_server->queue_render_pipeline(descriptor);

// 3. Retrieve the compiled pipeline each frame (returns error if not ready yet)
// Note in some cases this may also handled by plugins automatically, e.g. if using epix_render's higher-level render graph API
auto result = pipeline_server->get_render_pipeline(id);
if (!result) { return; }  // Queued or still compiling — check next frame
auto& pipeline = result.value().get();
```

**States a pipeline transitions through:** `PipelineStateQueued` → `PipelineStateCreating` (async future) → `Pipeline` (ready) or `PipelineServerError`.

> Custom module imports (bare identifier `import foo;`) still need to be registered before the first compile succeeds. An unregistered custom import produces a recoverable `ShaderCacheError` and the pipeline stays in `Queued` state until the import appears.

---

## Diagnosing Compile Errors

| Error stage | Meaning | Fix |
|-------------|---------|-----|
| `SessionCreation` | Bad options/target | Check SPIR-V target setup |
| `ModuleLoad` | Syntax / parse error | Check Slang syntax; look at `err.message()` for line |
| `Compose` | Entry point not found | Confirm `[shader("...")]` on the function |
| `Link` | Unresolved import | Register missing module in cache; check `import` path |
| `CodeGeneration` | SPIR-V capability issue | Replace `SV_InstanceID` → `SV_VulkanInstanceID`; check unsupported intrinsics |

**Recoverable errors** (return `is_recoverable() == true`): a custom-import dependency hasn't been registered yet. Safe to retry next frame.

---

## Checklist Before Submitting a Shader

- [ ] Module name: string-literal form if it contains `/` or `.`
- [ ] All exported structs and their members marked `public`
- [ ] Entry points have `[shader("vertex"|"fragment"|"compute")]`
- [ ] No `SV_InstanceID` — use `SV_VulkanInstanceID`
- [ ] File imports use `/` prefix (root-relative) or relative path — not bare names
- [ ] Custom module imports are registered in `ShaderCache` before `cache.get()`
- [ ] Preprocessor def names don't clash between shader-baked defs and caller defs
- [ ] `Handle<Shader>` kept alive (not dropped) for the lifetime of the pipeline
