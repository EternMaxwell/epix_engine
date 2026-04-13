# Shader Asset

Parsed shader asset, its source payload variant, and the app plugin that wires everything up.

## `ShaderPlugin`

### Overview

`ShaderPlugin` registers all shader-related asset support into the app. It adds the
`ShaderLoader` and `ShaderProcessor` to the asset system, and inserts a `ShaderCache`
resource bound to the render device so that pipelines can query compiled shader modules.

### Usage

```cpp
App app = App::create();
AssetPlugin{}.build(app);   // must come first
ShaderPlugin{}.build(app);
```

After `build()`, the app can load `.wgsl`, `.slang`, and `.slang-module` files via
`AssetServer::load<Shader>()`.

---

## `Source`

### Overview

Tagged union carrying the raw shader payload. Constructed via static factory functions.

| Inner type        | Content                                                       |
| ----------------- | ------------------------------------------------------------- |
| `Source::Wgsl`    | WGSL source text                                              |
| `Source::SpirV`   | SPIR-V bytecode `std::vector<uint8_t>`                        |
| `Source::Slang`   | Slang source text                                             |
| `Source::SlangIr` | Pre-compiled Slang IR blob (output of `IModule::serialize()`) |

### Usage

```cpp
// Create from text
auto wgsl = Source::wgsl("@vertex fn vs_main() -> @builtin(position) vec4f { return vec4f(); }");
auto slang = Source::slang("import utils; [shader(\"vertex\")] float4 vs_main() : SV_Position { return 0; }");

// Create from bytes
std::vector<uint8_t> spirv_bytes = load_spirv_from_file("shader.spv");
auto spirv = Source::spirv(std::move(spirv_bytes));

// Type predicates
if (src.is_wgsl()) { /* ... */ }

// Read text (WGSL or Slang only — undefined behaviour for SpirV)
std::string_view text = src.as_str();
```

---

## `Shader`

### Overview

Parsed shader asset. Created by `ShaderLoader`/`ShaderProcessor` from a file, or constructed
manually for embedded shaders.

| Field               | Type                     | Role                                                                                                          |
| ------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------- |
| `path`              | `AssetPath`              | Where the shader came from.                                                                                   |
| `source`            | `Source`                 | Raw payload.                                                                                                  |
| `import_path`       | `ShaderImport`           | How other shaders import this one. Falls back to `ShaderImport::asset_path(path)` when no custom name is set. |
| `imports`           | `vector<ShaderImport>`   | Imports declared by this shader.                                                                              |
| `shader_defs`       | `vector<ShaderDefVal>`   | Default definitions attached to this shader.                                                                  |
| `file_dependencies` | `vector<Handle<Shader>>` | File-backed imports resolved during loading.                                                                  |
| `validate_shader`   | `ValidateShader`         | Whether the backend runs validation.                                                                          |

### Usage — loading from file

The most common path is loading from the asset server:

```cpp
auto handle = server.load<Shader>(AssetPath("shaders/main.wgsl"));
```

Imports declared in the file are automatically resolved and loaded as asset dependencies.

### Usage — manual construction

```cpp
// WGSL from inline text
auto shader = Shader::from_wgsl(
    "@vertex fn vs_main() -> @builtin(position) vec4f { return vec4f(); }",
    AssetPath("embedded://vs_main.wgsl")
);

// WGSL with attached default definitions
auto shader = Shader::from_wgsl_with_defs(source, path, {ShaderDefVal::from_bool("USE_FOG")});

// Slang text
auto shader = Shader::from_slang(slang_source, AssetPath("shaders/lighting.slang"));

// SPIR-V bytes
auto shader = Shader::from_spirv(std::move(spirv_bytes), AssetPath("shaders/precompiled.spv"));

// Pre-compiled Slang IR blob
auto shader = Shader::from_slang_ir(std::move(ir_bytes), AssetPath("shaders/module.slang-module"));
```

### Usage — static preprocessing

`Shader::preprocess` and `Shader::preprocess_slang` parse a shader's own import name and
the list of imports it declares. `ShaderLoader` calls these automatically; they are exposed
for testing and tooling.

```cpp
auto [import_path, imports] = Shader::preprocess(wgsl_source, AssetPath("shaders/main.wgsl"));
// import_path: the name this shader exposes (from #define_import_path)
// imports: all #import directives found in the source
```

## Constraints / Gotchas

- `Source::as_str()` must only be called when `is_wgsl()` or `is_slang()` is true.
  Calling it on `SpirV` or `SlangIr` is undefined behaviour.
- Shaders added manually via `ShaderCache::set_shader()` can only be imported by custom
  name, not by asset path — they are not registered in the asset server.
- `file_dependencies` is populated only for file-backed imports resolved during loading.
  Custom-name imports remain as logical entries in `imports` and are resolved by
  `ShaderCache` at composition time.
