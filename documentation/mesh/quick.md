# Mesh module

`epix.mesh` provides move-only CPU mesh assets, GPU conversion, and the built-in 2D mesh renderer.

```cpp
import epix.mesh;
using namespace epix::mesh;
```

## CPU meshes

`Mesh` stores a WebGPU primitive topology, vertex attributes keyed by slot, and optional `uint16`
or `uint32` indices. Standard descriptors are `ATTRIBUTE_POSITION`, `ATTRIBUTE_COLOR`,
`ATTRIBUTE_NORMAL`, `ATTRIBUTE_UV0`, and `ATTRIBUTE_UV1`.

```cpp
Mesh mesh = Mesh(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::all())
                .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, positions)
                .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, uvs)
                .with_inserted_indices(Indices{std::vector<std::uint16_t>{0, 1, 2}});
auto handle = meshes.emplace(std::move(mesh));
```

The element byte size must equal `vertex_format_size(attribute.format)` or insertion returns
`MeshError::TypeIncompatible`. Attribute APIs include `insert_attribute`, `get_attribute[_mut]`,
`remove_attribute`, `iter_attributes[_mut]`, and builder-style `with_attribute` /
`with_removed_attribute`. `attribute_layout()` produces `MeshAttributeLayout`, whose lookup and
mutation methods operate on `MeshAttribute {name, slot, format}`.

Index APIs include `insert_indices`, `with_inserted_indices`, `indices`, `indices_mut`, `remove_indices`,
and `with_removed_indices`. `Indices` stores either a `std::vector<std::uint16_t>` or
`std::vector<std::uint32_t>`, exposes a lazy `iter()` range plus `len()` and `is_empty()`, and promotes
16-bit storage to 32-bit when `push()` or `extend()` receives an index above `UINT16_MAX`.
`get_index_buffer_bytes()` returns a borrowed byte span over the active index storage.
`MeshWindingInvertError` and `MeshTrianglesError` are tagged variants that preserve their specific
failure cause, including nested `MeshAccessError` values, and expose Bevy-compatible display text.
`duplicate_vertices()` expands every attribute in index order and removes the index buffer;
`invert_winding()` applies Bevy's topology-specific index reversal. Both operations provide fallible
forms for extracted meshes and consuming builder forms. The `mesh_algorithms` example renders their
indexed, duplicated, and inverted results side by side.
`count_vertices()` returns the shortest attribute length and warns when attribute counts disagree.
`MeshError` also reports missing slots, name mismatches, and requested type mismatches.

Factories `make_circle()`, `make_box2d()`, and `make_box2d_uv()` cover common 2D geometry.

`MeshPlugin` registers `Mesh` as an asset. `MeshRenderPlugin` adds render-asset
extraction/processing.

## GPU mesh

`RenderMesh` is the `RenderAsset<Mesh>::ProcessedAsset` used in `RenderAssets<Mesh>`: lightweight
CPU metadata only (vertex count, index info, interned vertex-buffer layout, topology). The GPU
vertex/index buffers live in the shared `MeshAllocator` slabs (`mesh_vertex_slice` /
`mesh_index_slice`), packed by `allocate_and_free_meshes`; draw commands bind those slices
directly.

## Built-in 2D renderer

Add `MeshRenderPlugin` after the normal render stack. A renderable entity needs `Mesh2d`, a
`Transform` (and therefore propagated `GlobalTransform`), and exactly one supported material:

```cpp
commands.spawn(
    Mesh2d{mesh_handle},
    MeshMaterial2d{.color = {1, 0.4f, 0.2f, 1}, .alpha_mode = MeshAlphaMode2d::Blend},
    transform::Transform::identity());
```

`MeshTextureMaterial2d` adds an `Image` handle and defaults to blending. `MeshAlphaMode2d` selects
`Opaque` or `Blend`; entities may also carry `render::camera::RenderLayer`. The plugin extracts
`ExtractedMesh2d`, queues opaque/transparent Core2D phases, batches compatible instances into
`MeshBatch`, uploads `MeshInstanceBuffer`, and registers the built-in binding/draw commands
`BindMesh2dInstances`, `BindMesh2dTexture`, and `DrawMesh2dBatch`. These render-world types are
extension points; ordinary users generally only construct the main-world components.
