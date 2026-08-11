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
Mesh mesh;
mesh.with_attribute(Mesh::ATTRIBUTE_POSITION, positions)
    .with_attribute(Mesh::ATTRIBUTE_UV0, uvs)
    .with_indices(indices);
auto handle = meshes.add(std::move(mesh));
```

The element byte size must equal `vertex_format_size(attribute.format)` or insertion returns
`MeshError::TypeIncompatible`. Attribute APIs include `insert_attribute`, `get_attribute[_mut]`,
`remove_attribute`, `iter_attributes[_mut]`, and builder-style `with_attribute` /
`with_removed_attribute`. `attribute_layout()` produces `MeshAttributeLayout`, whose lookup and
mutation methods operate on `MeshAttribute {name, slot, format}`.

Index APIs include `insert_indices`, `with_indices`, `get_indices`, `get_indices_mut`, `remove_indices`,
and `with_removed_indices`. `MeshIndices` exposes `is_u16/u32`, `as_u16/u32`, `size`, and `empty`.
`count_vertices()` returns the shortest attribute length and warns when attribute counts disagree.
`MeshError` also reports missing slots, name mismatches, and requested type mismatches.

Factories `make_circle()`, `make_box2d()`, and `make_box2d_uv()` cover common 2D geometry.

`MeshPlugin` registers `Mesh` as an asset. `MeshRenderPlugin` adds render-asset
extraction/processing.

## GPU mesh

`GPUMesh::create_from_mesh()` and `update_from_mesh()` upload a CPU mesh. Inspect it through
`vertex_count()`, `is_indexed()`, `primitive_type()`, `iter_attributes()`,
`contains_attribute()`, and `attribute_layout()`. `bind_to(render_pass)` binds its vertex and
optional index buffers.
`GPUMesh` is the `RenderAsset<Mesh>::ProcessedAsset` used in `RenderAssets<Mesh>`.

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
