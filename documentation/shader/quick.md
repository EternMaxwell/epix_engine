# Shader module

`epix.shader` turns WGSL, SPIR-V, Slang source, and processed Slang modules into
shader assets and compiled WebGPU modules. It covers four layers:

1. `ShaderLoader` reads files into `Shader` assets;
2. `ShaderProcessor` optionally preprocesses/compiles them in processed mode;
3. `ShaderCache` resolves imports and caches definition-specific modules; and
4. the renderer's `PipelineServer` uses that cache to create pipelines.

```cpp
import epix.assets;
import epix.shader;
```

## Normal application setup

With the renderer, add only `RenderPlugin`; it installs `ShaderPlugin` and owns
the `ShaderCache` through `PipelineServer`:

```cpp
app.add_plugins(render::RenderPlugin{});

Handle<shader::Shader> shader =
    app.world().resource<assets::AssetServer>().load<shader::Shader>(
        "shaders/sprite.wgsl");
```

Without the renderer, install asset and shader support yourself:

```cpp
app.add_plugins(assets::AssetPlugin{}, shader::ShaderPlugin{});
```

`ShaderPlugin` does not create a standalone `ShaderCache`. If an application
needs cache compilation without `PipelineServer`, construct and insert one with
a backend `LoadModuleFn`, then let the plugin's `Last` system synchronize shader
asset events into it.

## Load with definitions

```cpp
auto handle = server.load_with_settings<shader::Shader, shader::ShaderSettings>(
    "shaders/lighting.wgsl",
    [](shader::ShaderSettings& settings) {
        settings.shader_defs = {
            shader::ShaderDefVal::from_bool("USE_SHADOWS"),
            shader::ShaderDefVal::from_uint("CASCADE_COUNT", 4),
        };
    });
```

Handled extensions are `wgsl`, `spv`, `slang`, and `slang-module`. File imports
become asset dependencies; a shader reaches `LoadedWithDependencies` only after
those imports load.

## Manual shader and cache use

```cpp
Shader shader = Shader::from_wgsl(source, "embedded://runtime/main.wgsl");
AssetId<Shader> id = /* an ID owned by your integration */;

auto invalidated = cache.set_shader(id, std::move(shader));
auto module = cache.get(pipeline_id, id, extra_defs);
if (!module) {
    if (module.error().is_recoverable()) {
        // A shader/import is not available yet; retry after synchronization.
    } else {
        spdlog::error("{}", module.error().message());
    }
}
```

Do not try to drain `Events<T>` directly. In an ECS system, pass
`EventReader<AssetEvent<Shader>>::read()` to `ShaderCache::sync`; outside the
event pipeline, use `set_shader()` and `remove()` directly.

## Cheatbook

- [Shader asset, source variants, factories, and plugin](shader-asset.md)
- [Loader, settings, processor, and loader errors](shader-loading.md)
- [Imports and flexible shader references](shader-import.md)
- [Definition values and validation](shader-defs.md)
- [WGSL composition](shader-composer.md)
- [Runtime cache, invalidation, and errors](shader-cache.md)
