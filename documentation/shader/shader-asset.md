# Shader assets and plugin

`Shader` is the asset-side representation consumed by `ShaderCache` and the
renderer's `PipelineServer`.

## `ShaderPlugin`

```cpp
struct ShaderPlugin { void attach(app::App& app); };
```

Attachment registers `Assets<Shader>`, its events, and `ShaderLoader`. It also
adds a `Last` system that synchronizes dependency-ready/modified/unused shader
events into a `ShaderCache` when that resource exists. When `AssetProcessor` is
already present, it registers `ShaderProcessor`, assigns it as the default for
`wgsl`, `slang`, and `slang-module`, and maintains the processor's custom-module
registry.

```cpp
app.add_plugins(assets::AssetPlugin{}, shader::ShaderPlugin{});
```

Add asset processing before `ShaderPlugin` when processor registration is
required. In renderer applications, `RenderPlugin` installs `ShaderPlugin`
automatically and `PipelineServer` owns the operational cache.

## `Source`

`Source::data` is one of:

| Variant | Payload | Factory | Predicate |
| --- | --- | --- | --- |
| `Wgsl` | `std::string code` | `Source::wgsl` | `is_wgsl()` |
| `SpirV` | `vector<uint8_t> bytes` | `Source::spirv` | `is_spirv()` |
| `Slang` | `std::string code` | `Source::slang` | `is_slang()` |
| `SlangIr` | `vector<uint8_t> bytes` | `Source::slang_ir` | `is_slang_ir()` |

`as_str()` is valid only for WGSL and Slang text. Check the predicate before
calling it; byte-backed alternatives have no string view.

## `Shader`

| Field | Purpose |
| --- | --- |
| `path` | asset path associated with the source |
| `source` | text, bytecode, or processed IR |
| `import_path` | name/path by which other shaders import this shader |
| `imports` | declared dependencies |
| `shader_defs` | default definitions merged into compiled variants |
| `file_dependencies` | strong handles for loader-resolved file imports |
| `validate_shader` | `Disabled` or `Enabled` backend validation request |

Factories parse the appropriate import syntax:

```cpp
Shader::from_wgsl(text, path);
Shader::from_wgsl_with_defs(text, path, defs);
Shader::from_spirv(bytes, path);
Shader::from_slang(text, path);
Shader::from_slang_with_defs(text, path, defs);
Shader::from_slang_ir(bytes, path);
```

`preprocess(wgsl, path)` and `preprocess_slang(slang, path)` return the shader's
own `ShaderImport` plus its declared imports. Factories and the loader call them
automatically; they are useful for tools that need dependency discovery without
building a full asset server.

```cpp
auto shader = Shader::from_slang_with_defs(
    slang_source,
    "shaders/lighting.slang",
    {ShaderDefVal::from_bool("USE_FOG")});
shader.validate_shader = ValidateShader::Enabled;
```

Slang IR skips source import scanning because its graph is embedded in the IR.
Manually injected shaders may be resolved inside a cache by custom import name,
but they are not visible to `AssetServer` as file dependencies unless they were
loaded/registered through the asset pipeline.
