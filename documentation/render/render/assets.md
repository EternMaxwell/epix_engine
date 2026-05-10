# Render Assets

Render assets bridge the main-world asset system and GPU resources.  The
pattern is:

1. **Specialise** `RenderAsset<T>` to describe how `T` is converted to a GPU
   asset.
2. **Add** `ExtractAssetPlugin<T>` to wire the extraction and processing
   pipeline automatically.
3. **Access** processed GPU assets in render-world systems via
   `RenderAssets<T>`.

---

## RenderAsset<T>

```cpp
namespace epix::render {
template <typename T>
struct RenderAsset;  // specialise this
}
```

**Required members in a specialisation:**

```cpp
template <>
struct epix::render::RenderAsset<MyMesh> {
    // The GPU-side type produced by process()
    using ProcessedAsset = GPUMesh;

    // System parameters needed by process() — any core::system_param
    using Param = std::tuple<Res<wgpu::Device>, Res<wgpu::Queue>>;

    // Convert a MyMesh into a GPUMesh
    ProcessedAsset process(MyMesh&& asset, Param& param);

    // Declare where this asset is used
    RenderAssetUsage usage(const MyMesh& asset) noexcept;
};
```

`RenderAsset<T>` must be an empty struct (no data members).

### RenderAssetUsageBits

```cpp
enum RenderAssetUsageBits : uint8_t {
    MainWorld   = 1 << 0,  // keep the asset in the main world after extraction
    RenderWorld = 1 << 1,  // send the asset to the render world
};
using RenderAssetUsage = uint8_t;
```

Return `MainWorld | RenderWorld` from `usage()` to keep a copy in both
worlds (requires `T` to be copy-constructible).  Return only `RenderWorld`
to move the asset (removing it from the main world).

---

## RenderAssets<T>

```cpp
template <RenderAssetImpl T>
struct RenderAssets {
    using Type = typename RenderAsset<T>::ProcessedAsset;

    void   insert(const AssetId<T>& id, Type&& asset);
    bool   contains(const AssetId<T>& id) const;
    bool   remove(const AssetId<T>& id);
    Type&  get(const AssetId<T>& id);          // throws if not found
    Type*  try_get(const AssetId<T>& id);      // nullptr if not found
    const Type* try_get(const AssetId<T>& id) const;
    auto   iter()       -> view over (AssetId<T>, Type) pairs;
    auto   iter() const -> view over (AssetId<T>, Type) pairs (const);
};
```

A world resource in the render sub-app.  Keyed by `AssetId<T>`.

**Usage in a render system:**

```cpp
void render_meshes(Res<render::RenderAssets<MyMesh>> gpu_meshes,
                   Query<Item<const MeshHandle&>> entities,
                   ...) {
    for (auto&& [handle] : entities.iter()) {
        if (auto* gpu = gpu_meshes->try_get(handle.id()); gpu) {
            // bind gpu->vertex_buffer, issue draw call ...
        }
    }
}
```

---

## ExtractAssetPlugin<T>

```cpp
template <RenderAssetImpl T>
struct ExtractAssetPlugin {
    void build(App& app);
};
```

Registers two systems inside `ExtractSchedule` in the render sub-app:

| System | Set | Action |
|--------|-----|--------|
| `extract_assets<T>` | `ExtractAssetSet::Extract` | Reads `AssetEvent<T>` from the main world, clones or moves changed assets |
| `process_render_assets<T>` | `ExtractAssetSet::Process` | Calls `RenderAsset<T>::process()` and stores results in `RenderAssets<T>` |

The `Extract` and `Process` sets run in order (chained).

```cpp
// In MyPlugin::build():
app.add_plugins(render::ExtractAssetPlugin<MyMesh>{});
```

### ExtractAssetSet

```cpp
enum class ExtractAssetSet {
    Extract,   // extracts changed assets from the main world
    Process,   // processes them into GPU resources
};
```

---

## GPUImage

A built-in `RenderAsset<image::Image>` specialisation:

```cpp
struct DefaultImageSampler {
    wgpu::Sampler sampler;
};

struct GPUImage {
    wgpu::Texture     texture;
    wgpu::TextureView view;
    wgpu::Sampler     sampler;
};

template <>
struct RenderAsset<epix::image::Image> {
    using ProcessedAsset = GPUImage;
    using Param = std::tuple<Res<wgpu::Device>, Res<wgpu::Queue>,
                             Res<DefaultImageSampler>>;
    ProcessedAsset process(image::Image&& asset, Param param);
    RenderAssetUsage usage(const image::Image& asset) noexcept;
};
```

Provided automatically.  Access GPU images in render systems via
`Res<render::RenderAssets<image::Image>>`.
