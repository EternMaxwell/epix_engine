---
name: webgpu-wrapper-usage
description: "Use when writing or modifying code with our custom WebGPU C++ wrapper (libs/webgpu-wrapper/). Covers SmallVec struct array fields, callback lifetime and ref-count rules, and enum value style. Apply whenever creating or modifying wgpu:: handles, descriptors, callbacks, feature/format lists, or any wgpu:: API calls."
---

# WebGPU C++ Wrapper Usage

## Source Files

| File | Purpose |
|------|---------|
| `libs/webgpu-wrapper/generate.py` | **Primary reference** — the generator script; reads the WebGPU spec and template to produce the final module |
| `libs/webgpu-wrapper/webgpu.template.cppm` | Template used by the generator — controls placement of generated sections and adds extra injected methods/constructors; read this to understand what extra functionality is added on top of the spec |
| `cmake/webgpu_gen_module.cmake` | CMake function that invokes the generator with the project-specific arguments (see below) |
| `build/generated/webgpu/webgpu.cppm` | **Generated output** — the actual compiled module; consult this to see the final API shape (available types, enum entries, handle methods) |
| `libs/webgpu-wrapper/small_vec.cppm` | `SmallVec<T, N>` implementation |

> Do **not** read `libs/webgpu-wrapper/webgpu.cppm` — it is generated with different arguments than the one actually compiled into the engine. Always look at `build/generated/webgpu/webgpu.cppm` for the real generated API.

### How `build/generated/webgpu/webgpu.cppm` is generated

The CMake function `generate_webgpu_wrapper` in `cmake/webgpu_gen_module.cmake` invokes the generator as:

```sh
python generate.py \
  -i <webgpu-header(s)> \
  -t libs/webgpu-wrapper/webgpu.template.cppm \
  -o build/generated/webgpu/webgpu.cppm \
  --use-raii \
  --indexed-handle BindGroupLayout
```

Key arguments that differ from other builds in `libs/webgpu-cpp/`:
- `--use-raii` — generates RAII `wgpu::*` handle wrappers (copy = addRef, move = transfer)
- `--indexed-handle BindGroupLayout` — only `BindGroupLayout` gets indexed-handle treatment (not `all`)

---

## Rule 1 — `SmallVec` for Array Fields in Descriptor Structs

Array-typed fields in `wgpu` descriptor structs use `SmallVec<T>`, **not** `std::span`, `std::vector`, or raw pointer+count pairs.

```cpp
// ✅ correct
wgpu::PipelineLayoutDescriptor desc;
desc.bindGroupLayouts = {layout0, layout1};  // SmallVec initializer-list

wgpu::DeviceDescriptor deviceDesc;
deviceDesc.requiredFeatures = {wgpu::FeatureName::eTimestampQuery};

wgpu::SurfaceCapabilities caps = surface.getCapabilities(adapter);
for (auto& fmt : caps.formats) { ... }  // SmallVec<wgpu::TextureFormat>
```

```cpp
// ❌ wrong — std::span dangles after the source array goes out of scope
std::array<wgpu::BindGroupLayout, 2> layouts = {layout0, layout1};
desc.bindGroupLayouts = std::span(layouts);  // dangling after 'layouts' is gone
```

**Why**: `SmallVec<T, N=4>` stores up to N elements on the stack and spills to the heap beyond that. Descriptor structs must be safely movable and copyable out of scope; `std::span` would dangle.

---

## Rule 2 — Callback Lifetime and Ref-Count

Callback wrapper types (e.g., `wgpu::BufferMapCallback`) are ref-counted objects with a `Control` base that tracks:
- `count` — reference count (starts at 1)
- `invoke_times` — atomic invocation counter (starts at 0)

When a `*CallbackInfo` struct is submitted via `to_cstruct()`, a **copy** of the callback is captured into the C userdata. On **first invocation only** (`invoke_times.fetch_add(1) == 0`), the captured copy calls `reset()`, releasing that ref.

### Implication

If the `*CallbackInfo` struct holding the original callback goes out of scope **before** the GPU fires the callback, the captured copy is the only remaining reference, and it will be freed after invocation — this is fine for one-shot fire-and-forget callbacks.

If you need to **inspect, cancel, or re-use** the callback after submission, retain the wrapper:

```cpp
// ✅ Keep callback alive across async gap
struct MyResource {
    wgpu::BufferMapCallback mapCb;
};

// In a system:
res.mapCb = wgpu::BufferMapCallback([](WGPUMapAsyncStatus status, WGPUStringView msg) {
    // handle result
});

wgpu::BufferMapCallbackInfo info;
info.callback = res.mapCb;  // copy → ref count +1; resource holds the other ref
info.mode = wgpu::CallbackMode::eAllowProcessEvents;
buffer.mapAsync2(wgpu::MapMode::eRead, 0, size, info);
// res.mapCb keeps the callback alive until the resource is destroyed
```

```cpp
// ✅ One-shot: local callback is fine — the captured copy survives invocation
{
    wgpu::BufferMapCallbackInfo info;
    info.callback = wgpu::BufferMapCallback([](WGPUMapAsyncStatus, WGPUStringView) {});
    info.mode = wgpu::CallbackMode::eAllowProcessEvents;
    buffer.mapAsync2(wgpu::MapMode::eRead, 0, size, info);
    // info goes out of scope; captured copy in userdata is the sole ref — fine
}
```

---

## Rule 3 — Enum Values: Always Use Declared Entries

All WebGPU enum values are declared as `ePascalCase` members. **Never** cast numeric literals to enum types.

```cpp
// ✅ correct
wgpu::TextureFormat fmt = wgpu::TextureFormat::eRGBA8Unorm;
wgpu::BufferUsage usage = wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst;
wgpu::ShaderStage stages = wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment;
```

```cpp
// ❌ wrong — never use numeric casts
wgpu::FeatureName feat = wgpu::FeatureName(0x00030002u);
wgpu::TextureFormat fmt = static_cast<wgpu::TextureFormat>(23);
```

Flag enums (`BufferUsage`, `TextureUsage`, `ShaderStage`, `ColorWriteMask`, etc.) support `operator|` directly.

To find all enum entries for a given type, read `build/generated/webgpu/webgpu.cppm` and search for the enum name.

---

## Additional Notes

### Handle Types — Prefer RAII `wgpu::*`

Use `wgpu::*` RAII handles (e.g., `wgpu::Buffer`, `wgpu::Device`) rather than `wgpu::raw::*` handles unless you are explicitly managing lifetime manually.

- `wgpu::*` copy → `addRef()`, destructor → `release()`
- `wgpu::*` move → transfers ownership, source becomes null
- `wgpu::raw::*` → thin manual-ref wrapper; analogous to a raw pointer — you own the ref count explicitly

### `StringView`

`wgpu::StringView` accepts both `const char*` (uses `WGPU_STRLEN` sentinel for null-terminated) and `std::string_view`. It is implicitly constructible from either:

```cpp
desc.label = "my buffer";          // const char* — null-terminated
desc.label = std::string_view(s);  // string_view with explicit length
```

### `NextInChain<T>` — Extension Structs

Use `NextInChain<T>` to attach chained extension structs. It is ref-counted and type-safe:

```cpp
wgpu::DeviceDescriptor desc;
auto ext = std::make_shared<wgpu::SomeExtension>();
desc.nextInChain.setNext(ext);
```
