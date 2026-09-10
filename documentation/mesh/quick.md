# Mesh module

`epix.mesh` provides move-only CPU mesh assets, GPU conversion, and the built-in 2D mesh renderer.

```cpp
import epix.mesh;
using namespace epix::mesh;
```

## CPU meshes

`Mesh` stores a WebGPU primitive topology, vertex attributes keyed by stable attribute ID, and optional
`uint16` or `uint32` indices. Each attribute pairs its descriptor with a `VertexAttributeValues` value.
Standard descriptors are `ATTRIBUTE_POSITION`, `ATTRIBUTE_COLOR`, `ATTRIBUTE_NORMAL`, `ATTRIBUTE_UV0`,
and `ATTRIBUTE_UV1`.

`VertexAttributeValues` provides Bevy's 28 semantically distinct vertex-value alternatives. It
preserves distinctions such as `Sint16x2` versus `Snorm16x2` even though both use the same C++
element representation, reports the matching WebGPU format, exposes borrowed float3 and raw-byte
views, and supports fallible conversion back to scalar, `std::array`, and GLM vector collections.
Epix uses `glm::vec3` for both of Bevy's `Vec3`/`Vec3A` conversion roles because GLM has one standard
three-component float-vector type.

```cpp
Mesh mesh = Mesh(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::all())
                .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, positions)
                .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, uvs)
                .with_inserted_indices(Indices{std::vector<std::uint16_t>{0, 1, 2}});
auto handle = meshes.emplace(std::move(mesh));
```

Insertion converts supported C++ ranges to `VertexAttributeValues` and requires its semantic format to
equal the descriptor format. Thus identically sized formats such as `Sint32x3` and `Float32x3` are not
interchangeable; a mismatch is a programmer error and throws `std::invalid_argument`, as in Bevy.
Attribute APIs include `insert_attribute`, `attribute` / `attribute_mut`, `remove_attribute`,
`attributes` / `attributes_mut`, and their fallible `try_` forms. Accessors and removals expose
`VertexAttributeValues`, while the rvalue-qualified `with_inserted_attribute` builder returns the
completed mesh by value. `attribute_layout()` produces `MeshAttributeLayout`, whose lookup and mutation
methods operate on `MeshAttribute {name, slot, format}`.

Index APIs include `insert_indices`, `with_inserted_indices`, `indices`, `indices_mut`, `remove_indices`,
and `with_removed_indices`. `Indices` stores either a `std::vector<std::uint16_t>` or
`std::vector<std::uint32_t>`, exposes a lazy `iter()` range plus `len()` and `is_empty()`, and promotes
16-bit storage to 32-bit when `push()` or `extend()` receives an index above `UINT16_MAX`.
`get_index_buffer_bytes()` returns a borrowed byte span over the active index storage.
`MeshWindingInvertError` and `MeshTrianglesError` are tagged variants that preserve their specific
failure cause, including nested `MeshAccessError` values, and expose Bevy-compatible display text.
`duplicate_vertices()` expands every attribute in index order and removes the index buffer;
`invert_winding()` applies Bevy's topology-specific index reversal. Both operations provide fallible
forms for extracted meshes and consuming builder forms. `triangles()` returns an expected lazy range
over indexed triangle-list or triangle-strip faces; incomplete lists are truncated and faces with
out-of-range indices are skipped. The `mesh_algorithms` example exercises the range and renders the
indexed, duplicated, and inverted results side by side.
`count_vertices()` returns the shortest attribute length and warns when attribute counts disagree.
`MeshError` also reports missing slots, name mismatches, and requested type mismatches.

## Primitive builders

`MeshBuilder` and `Meshable` are C++ concepts corresponding to Bevy's mesh-construction traits. A
builder supplies `build()`, a shape supplies `mesh()`, and either converts directly to `Mesh`.
The foundational 2D set includes `Circle`, `Ellipse`, `RegularPolygon`, and `Rectangle` with their
matching builders:

```cpp
Mesh circle = Circle{80.0f};
Mesh ellipse = Ellipse{105.0f, 70.0f}.mesh().with_resolution(48);
Mesh hexagon = RegularPolygon{85.0f, 6};
Mesh rectangle = Rectangle{170.0f, 120.0f};
```

These builders match Bevy's defaults and generated position, +Z normal, UV, and U32 index data.
Shapes live in `epix::mesh` because Epix has no separate math-primitives module. Circle and ellipse
builders retain Bevy's public `resolution` field; their consuming fluent method is named
`with_resolution()` because C++ cannot declare a field and method with the same name.

`CircularSector` and `CircularSegment` provide the matching +Y-symmetric arc and chord inputs.
Their builders support `CircularMeshUvMode::Mask`, including UV rotation:

```cpp
Mesh sector = CircularSector::from_degrees(100.0f, 240.0f).mesh().with_resolution(48);
Mesh segment = CircularSegment::from_degrees(100.0f, 120.0f)
                   .mesh()
                   .with_resolution(48)
                   .with_uv_mode(CircularMeshUvMode::Mask{.angle = 0.35f});
```

The fluent UV setter is `with_uv_mode()` for the same C++ field/method naming reason.

The older convenience factories `make_circle()`, `make_box2d()`, and `make_box2d_uv()` remain available
for existing Epix code.

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
