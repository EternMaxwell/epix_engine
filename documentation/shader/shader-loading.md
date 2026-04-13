# Shader Loading

Asset loader, processor, and their error types for the shader asset pipeline.

## `ShaderLoader`

### Overview

`AssetLoader` implementation that reads `.wgsl`, `.slang`, and `.slang-module` files into
`Shader` assets. Registered automatically by `ShaderPlugin`.

| Associated type | Value               |
| --------------- | ------------------- |
| `Asset`         | `Shader`            |
| `Settings`      | `ShaderSettings`    |
| `Error`         | `ShaderLoaderError` |

### Usage

`ShaderLoader` is invoked automatically by the asset server. The only integration point is
`ShaderSettings`, which can be passed via `AssetServer::load_with_settings()`:

```cpp
auto handle = server.load_with_settings<Shader>(
    AssetPath("shaders/lighting.wgsl"),
    ShaderSettings{
        .shader_defs = {ShaderDefVal::from_bool("USE_SHADOWS")}
    }
);
```

The loaded `Shader` will have those definitions merged into `shader_defs`.

### File extensions

`ShaderLoader::extensions()` returns the list of handled extensions:
`.wgsl`, `.slang`, `.slang-module`.

---

## `ShaderSettings`

### Overview

Loader settings passed to `ShaderLoader` when loading a shader asset.

| Field         | Type                   | Default | Meaning                                          |
| ------------- | ---------------------- | ------- | ------------------------------------------------ |
| `shader_defs` | `vector<ShaderDefVal>` | `{}`    | Extra definitions attached to the loaded shader. |

---

## `ShaderProcessor`

### Overview

`AssetProcessor` implementation that pre-processes shader sources before loading.
Registered automatically by `ShaderPlugin`.

| Associated type | Value                     |
| --------------- | ------------------------- |
| `Settings`      | `ShaderProcessorSettings` |
| `OutputLoader`  | `ShaderLoader`            |

The processor runs when the asset pipeline is in `Processed` mode. It handles:
- WGSL source preprocessing (import path extraction)
- Slang source preprocessing (import path extraction)
- Optional ahead-of-time Slang-to-IR compilation (stores a `.slang-module` blob as the processed form)

### Usage

`ShaderProcessor` is invoked automatically by the asset pipeline in Processed mode. To
control its behaviour, pass `ShaderProcessorSettings` as the asset meta settings.

```cpp
// Default: WGSL preprocessing and Slang preprocessing enabled,
//          Slang-to-IR compilation enabled.
ShaderProcessorSettings defaults;

// Disable ahead-of-time IR compilation (keep Slang as text):
ShaderProcessorSettings settings;
settings.preprocess_slang_to_ir = false;
```

---

## `ShaderProcessorSettings`

### Overview

Settings controlling what the `ShaderProcessor` does for a given shader.

| Field                    | Type             | Default | Meaning                                                                                                       |
| ------------------------ | ---------------- | ------- | ------------------------------------------------------------------------------------------------------------- |
| `loader_settings`        | `ShaderSettings` | `{}`    | Forwarded to `ShaderLoader`.                                                                                  |
| `preprocess_wgsl`        | `bool`           | `true`  | Run WGSL import-path extraction.                                                                              |
| `preprocess_slang`       | `bool`           | `true`  | Run Slang import-path extraction.                                                                             |
| `preprocess_slang_to_ir` | `bool`           | `true`  | Compile `.slang` to a Slang IR blob during processing; falls back to text preprocessing if compilation fails. |

---

## `ShaderLoaderError`

### Overview

Error returned by `ShaderLoader::load()`.

| Variant                    | Meaning                                                   |
| -------------------------- | --------------------------------------------------------- |
| `ShaderLoaderError::Io`    | File read failed; holds `error_code` + `path`.            |
| `ShaderLoaderError::Parse` | Source could not be parsed; holds `path` + `byte_offset`. |

### Usage

```cpp
auto result = ShaderLoader::load(stream, settings, context);
if (!result.has_value()) {
    const auto& err = result.error();
    if (auto* io = std::get_if<ShaderLoaderError::Io>(&err.data)) {
        spdlog::error("IO error on {}: {}", io->path.string(), io->code.message());
    } else if (auto* parse = std::get_if<ShaderLoaderError::Parse>(&err.data)) {
        spdlog::error("parse error in {} at byte {}", parse->path.string(), parse->byte_offset);
    }
}
```

### `to_exception_ptr`

Free function `to_exception_ptr(const ShaderLoaderError&)` converts a `ShaderLoaderError`
into a `std::exception_ptr` for use with the asset system's error propagation:

```cpp
auto exc = to_exception_ptr(err);
std::rethrow_exception(exc);
```
