# Shader Composer

WGSL-only `#import` expander and conditional block evaluator.

## `ShaderComposer`

### Overview

`ShaderComposer` maintains a registry of named WGSL modules and produces a single final
WGSL string by expanding `#import` directives and evaluating `#ifdef`/`#ifndef`/`#if`/
`#else`/`#endif` blocks.

This is used internally by `ShaderCache` for WGSL shaders. It is also available standalone
for offline tooling or unit tests.

Slang shaders use Slang's own import system — `ShaderComposer` does not process them.

### Usage

```cpp
ShaderComposer composer;

// Register a named WGSL module
ASSERT_TRUE(
    composer.add_module("lighting::core", "fn ambient() -> f32 { return 0.05; }", {})
            .has_value()
);

// Compose a shader that imports the module, with extra definitions active
const char* source =
    "#import lighting::core\n"
    "#ifdef USE_SHADOWS\n"
    "fn shadow_factor() -> f32 { return 0.5; }\n"
    "#endif\n"
    "@fragment fn fs_main() -> @location(0) vec4f {\n"
    "    let a = ambient();\n"
    "    return vec4f(a, a, a, 1.0);\n"
    "}\n";

const std::array defs = {ShaderDefVal::from_bool("USE_SHADOWS")};
auto result = composer.compose(source, "main.wgsl", defs);

if (result.has_value()) {
    std::string final_wgsl = *result;  // ready to pass to wgpu::Device::createShaderModule
} else {
    // Handle ComposeError
}
```

### Methods

| Method                             | Description                                                                                |
| ---------------------------------- | ------------------------------------------------------------------------------------------ |
| `add_module(name, source, defs)`   | Register a named module. Returns a `ComposeError` if the source is malformed.              |
| `remove_module(name)`              | Unregister a module; no-op if not found.                                                   |
| `contains_module(name)`            | Returns `true` when a module with this name is registered.                                 |
| `compose(source, file_path, defs)` | Expand imports and evaluate conditionals. Returns the final WGSL text or a `ComposeError`. |

### Constraints / Gotchas

- Circular imports (e.g. `A` imports `B` which imports `A`) are detected and reported as
  `ComposeError::CircularImport`.
- `defs` passed to `compose()` are merged with each module's own `defs` during composition.
- Module names are case-sensitive. `#import ui::button` only matches a module registered as
  `"ui/button"` (separators are normalized to `/`).

---

## `ComposeError`

### Overview

Error returned by `ShaderComposer::add_module()` and `ShaderComposer::compose()`.

| Variant                        | Meaning                                                                   |
| ------------------------------ | ------------------------------------------------------------------------- |
| `ComposeError::ImportNotFound` | A `#import` target was not registered. Holds the missing `import_name`.   |
| `ComposeError::ParseError`     | Source could not be parsed. Holds `module_name` and `details`.            |
| `ComposeError::CircularImport` | A dependency cycle was detected. Holds the `cycle_chain` of module names. |

### Usage

```cpp
auto result = composer.compose(source, "main.wgsl", defs);
if (!result) {
    std::visit([](auto&& err) {
        using T = std::decay_t<decltype(err)>;
        if constexpr (std::is_same_v<T, ComposeError::ImportNotFound>) {
            spdlog::warn("missing module: {}", err.import_name);
        } else if constexpr (std::is_same_v<T, ComposeError::CircularImport>) {
            spdlog::error("circular import chain detected");
        }
    }, result.error().data);
}
```
