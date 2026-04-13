# Shader Definitions

Types that control conditional compilation and backend validation.

## `ShaderDefVal`

### Overview

A named definition with a boolean, signed integer, or unsigned integer value.
Definition sets are passed to `ShaderCache::get()` to select a compiled shader variant,
and to `ShaderComposer::compose()` to evaluate `#ifdef`/`#ifndef`/`#if` blocks.

### Usage

```cpp
// Boolean (defaults to true)
auto flag      = ShaderDefVal::from_bool("USE_FOG");
auto disabled  = ShaderDefVal::from_bool("USE_FOG", false);

// Signed integer
auto lod       = ShaderDefVal::from_int("LOD_LEVEL", -1);

// Unsigned integer
auto samples   = ShaderDefVal::from_uint("MSAA_SAMPLES", 4);

// Get the string representation (used during shader composition)
std::string text = samples.value_as_string();  // "4"
std::string text = flag.value_as_string();     // "true"
```

Definitions are compared by both name and value; two `ShaderDefVal` with the same name but
different values are not equal.

---

## `ValidateShader`

### Overview

Controls whether backend shader validation runs when `ShaderCache` calls the
`LoadModuleFn` to create a `wgpu::ShaderModule`.

| Enumerator                 | Meaning                            |
| -------------------------- | ---------------------------------- |
| `ValidateShader::Disabled` | Skip backend validation (default). |
| `ValidateShader::Enabled`  | Request backend validation.        |

### Usage

```cpp
// Attach to a Shader asset to override the default:
shader.validate_shader = ValidateShader::Enabled;
```

The value is forwarded verbatim to the `LoadModuleFn` callback supplied to `ShaderCache`.
The backend interprets it; WebGPU typically runs shader validation when enabled.
