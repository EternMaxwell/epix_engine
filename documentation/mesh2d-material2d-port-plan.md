# Mesh2d / Material2d Port Plan (S9 + S10)

Work order for replacing Epix's hand-rolled Mesh2d renderer with Bevy 0.18's
generic `Material2d` pipeline. Rows S9 and S10 of
`documentation/mesh-core-graph-bevy-0.18-match-status.md` are the tracker
entries; this file is the execution order and is expected to be updated as each
step lands.

Pinned sources: `tmp/bevy-0.18.0/crates/bevy_sprite_render/src/mesh2d/mesh.rs`
(829 lines) and `.../mesh2d/material.rs` (1027 lines), plus
`.../mesh2d/color_material.rs`.

## Why this is a replacement, not an addition

`sprite_render/src/mesh2d.cpp` is 891 lines built around **eight hand-written
embedded Slang shader variants** (solid / vertex-colour / textured /
textured-vertex-colour vertices crossed with colour and mask fragments) and a
file-local `Mesh2dPipelineKey { MeshShaderVariant variant; AlphaMode alpha_mode;
uintptr_t layout_id; }`. Bevy has **one** embedded `mesh2d.wgsl` vertex/fragment
pair driven by a bitflags `Mesh2dPipelineKey`, with the *material* supplying the
fragment shader. Both designs cannot coexist: they would queue the same meshes.

## Hard constraints

1. **The component swap is atomic.** `MeshMaterial2d` is currently a fixed
   aggregate (`{.color, .alpha_mode}`) with **65** construction sites and
   `MeshTextureMaterial2d` with **9**, spread over **33 files** including every
   header/module twin. C++ cannot have a class and a class template share a
   name, so `MeshMaterial2d<M>` and the site migration land in one commit, and
   each migrated site additionally needs an `Assets<ColorMaterial>` source and a
   `ColorMaterialPlugin` registration.
2. **Order is forced.** `Material2dPipeline<M>` embeds Bevy's `Mesh2dPipeline`,
   and `PreparedMaterial2d<M>::Param` names `Material2dPipeline<M>`, so the
   mesh2d pipeline lands first.
3. **Adaptations already settled**: no `bevy_color::Color` in Epix (use
   `glm::vec4`), no `Affine2` (use `glm::mat3`), Slang instead of WGSL, and
   direct wgpu types where the accepted convention allows.

## Steps

Each step must leave the tree building and the affected examples rendering
unchanged until step 10.

| # | Step | Bevy anchor | Epix target |
| --- | --- | --- | --- |
| 1 | `Mesh2dPipelineKey` bitflags (MSAA 3 bits at the top, tonemap-method bits, `HDR`, `TONEMAP_IN_SHADER`, `DEBAND_DITHER`, primitive-topology and strip-index bits) with `from_msaa_samples`/`msaa_samples`/`from_hdr` and the topology/strip helpers | `mesh.rs:419-494` | **DONE** (`b739f4c4`): public key in `sprite_render/include/epix/sprite_render/mesh2d_pipeline.hpp` with Bevy's exact layout and the five helpers, covered by `tests/header/mesh2d_pipeline_key_tests.cpp` (4 tests). The file-local variant key is retired in step 10. Note: Bevy's key also carries `BLEND_ALPHA` (1<<3) and `MAY_DISCARD` (1<<4), which the port includes; there is no separate "strip index" bit - the strip/indexed information lives in the topology bits and the mesh's own index format. |
| 2 | `Mesh2dUniform`, `Mesh2dTransforms`, `Mesh2dPipeline { view_layout, mesh_layout, shader, per_object_buffer_batch_size }`, `init_mesh_2d_pipeline` (view layout = dynamic `ViewUniform` + `GlobalsUniform` + LUT texture/sampler at fragment; mesh layout = the mesh uniform array) | `mesh.rs:193-227`, `285-327` | `render::mesh`-level uniform types + `sprite_render` pipeline resource |
| 3 | `Mesh2dBindGroup`/`prepare_mesh2d_bind_group`, `Mesh2dViewBindGroup`/`prepare_mesh2d_view_bind_groups` | `mesh.rs:653-718` | render-world resources/component. **Step 3 is additive and must leave the legacy draw path alone.** An attempt on 2026-09-17 added `Mesh2dViewBindGroup` + `prepare_mesh2d_view_bind_groups` *and* switched the legacy mesh2d draw registrations to `SetMesh2dViewBindGroup<0>`, which broke `mesh_rendering`: the legacy pipeline still draws, its group 0 is the one-binding non-dynamic shared view layout, so binding the new four-binding dynamic mesh2d view group fails validation ("expects 1 dynamic offset. However 0 dynamic offset were provided", then a layout incompatibility). The machinery was reverted. Add the components and prepares here; the registrations switch in step 5 (draw commands) and step 9 (plugin wiring). |
| 4 | `RenderMesh2dInstance`/`RenderMesh2dInstances` (main-entity keyed, `material_bind_group_id`, `mesh_asset_id`, `transforms`, `tag`, `automatic_batching`) and `extract_mesh2d` | `mesh.rs:235-283`, `249` | replaces `ExtractedMesh2d`/`RenderMesh2dInstances` |
| 5 | `SetMesh2dViewBindGroup`/`SetMesh2dBindGroup`/`DrawMesh2d` and the `GetBatchData for Mesh2dPipeline` integration (`CompareData = (Material2dBindGroupId, AssetId<Mesh>)`) | `mesh.rs:329-351`, `719-800` | replaces the current draw commands and `MeshBatch` |
| 6 | `Material2d` trait + `Material2dImpl`, `Material2dKey<M>`, `PreparedMaterial2d<M>`, `Material2dPipeline<M>` | `material.rs:131-167`, `382-420` | `sprite_render` material surface |
| 7 | `ColorMaterial`, `ColorMaterialFlags`, `ColorMaterialUniform`, `AsBindGroup<ColorMaterial>`, `AsBindGroupShaderType<ColorMaterial, ColorMaterialUniform>`, `Material2d<ColorMaterial>` and the Slang `color_material` shader | `color_material.rs` (157 lines), `color_material.wgsl` | `glm::vec4` colour, `glm::mat3` uv transform |
| 8 | `Material2dPlugin<M>`, `EntitiesNeedingSpecialization<M>`, `EntitySpecializationTickPair<M>`, `SpecializedMaterial2dPipelineCache<M>`, `SpecializedMaterial2dViewPipelineCache<M>`, `RenderMaterial2dInstances<M>`, `check_entities_needing_specialization<M>`, `extract_entities_needs_specialization<M>`, `extract_mesh_materials_2d<M>`, `specialize_material2d_meshes<M>`, `queue_material2d_meshes<M>` | `material.rs:263-378`, `556-700` | `sprite_render` plugin |
| 9 | `Mesh2dRenderPlugin` wiring, `ViewKeyCache`/`ViewSpecializationTicks`/`check_views_need_specialization`, `Mesh2dMarker` | `mesh.rs:57-116`, `117-158` | replaces `MeshRenderPlugin` |
| 10 | **Atomic swap**: `MeshMaterial2d<M>` replaces the fixed aggregate; migrate 65 + 9 sites over 33 files (twins kept in step); add `ColorMaterialPlugin`/`Assets<ColorMaterial>` to each affected example; retire the eight-variant shaders and the file-local key | `material.rs:199-233` | engine + examples |

## Landed steps

### Step 1 — `Mesh2dPipelineKey` (`b739f4c4`)

- **Files**: `sprite_render/include/epix/sprite_render/mesh2d_pipeline.hpp` (new),
  `sprite_render/include/epix/sprite_render.hpp` (umbrella), 
  `sprite_render/tests/header/mesh2d_pipeline_key_tests.cpp` (new).
- **Tests**: the key test target passes 4/4 (bit positions of all eight
  tonemapping methods, the three field shifts, MSAA/HDR round trips,
  primitive-topology round trips plus the unknown-pattern fallback, and
  contains/intersection/union); the full suite passes **81/81** in 46.15 s.
- **Rendering**: the change is additive - nothing consumes the key yet - so the
  verification is a non-regression check on the examples that render Mesh2d.
  Captured with `PrintWindow(PW_CLIENTONLY)` and inspected with the image tool:
  `mesh_rendering` (opaque square, two gradients, overlapping circle/square),
  `mesh_batching` (the 3000 opaque + 300 transparent pressure scene),
  `mesh_alpha_mask` (cyan/orange checker), `primitive_builders`
  (circle/ellipse/hexagon/rectangle) and `linear_polygon_primitives`
  (segment/convex polygon/polyline). Every example's scene crop below the
  overlay is byte-identical across all three frames and every stdout/stderr log
  is free of error, fatal, validation and panic matches.
- **Note**: a crop at y=130 reported two distinct scene hashes for
  `mesh_rendering`; the overlay's framerate graph extends below that line. Crops
  from y=200 are stable for all five, so the earlier difference was overlay
  leakage, not scene animation.
- **Dedicated example**: none yet by design. The key has no visible behaviour of
  its own; the dedicated material2d example and the per-example before/after
  captures belong to step 10, where the mesh2d pipeline and the material asset
  actually change what is drawn. Steps 2-9 will each carry their own
  affected-example captures.

### Step 2 — `Mesh2dUniform` and `Mesh2dTransforms` (partial)

- **Files**: `sprite_render/include/epix/sprite_render/mesh2d_pipeline.hpp`,
  `sprite_render/tests/header/mesh2d_uniform_tests.cpp` (new).
- **Landed**: `MeshFlags`, `Mesh2dTransforms` and `Mesh2dUniform` with Bevy's
  packing (`Affine3::to_transpose` rows plus the packed inverse-transpose 3x3),
  `static_assert`s for the WGSL array stride (96 bytes) and the 0/48/80/84/88
  offsets, and an `unpack_local_from_world_transpose` helper that reproduces the
  shader's `mat2x4_f32_to_mat3x3_unpack` column order.
- **Bug the tests caught**: the first packing wrote the mat4 *columns* into
  `world_from_local`. Bevy's `Affine3::to_transpose` stores the *rows* - component
  `i` of every column plus translation `i` - which the shader's
  `affine3_to_square` transposes back, so the column form would have rendered the
  transpose of every mesh2d transform. The rotation round-trip test failed on
  exactly that swap and the packing was fixed before commit. (A translation-only
  or diagonal-scale transform cannot distinguish the two, which is why the
  rotation case matters.)
- **Tests**: the uniform target passes 4/4 (stride/offsets, translation-only
  packing, non-uniform scale inverse-transpose, and a rotation round trip that
  rebuilds both the inverse-transpose 3x3 and the affine square). Full suite
  **82/82** in 46.73 s.
- **Rendering**: the same non-regression sweep as step 1 - `mesh_rendering`,
  `mesh_batching`, `mesh_alpha_mask`, `primitive_builders` and
  `linear_polygon_primitives` captured with `PrintWindow(PW_CLIENTONLY)`,
  inspected, each stable at one distinct cropped scene hash across three frames
  and free of flagged stdout/stderr diagnostics.
- **Remaining in step 2**: the embedded mesh2d Slang shader modules and the
  `init_mesh_2d_pipeline` system that loads them. Bevy's mesh2d view bindings
  import `bevy_render::globals::Globals`, and Epix has no `epix/globals` Slang
  library yet (only `epix.view`, `epix.tonemapping` and the svo ones), so that
  library is part of this remaining work and is a render-module addition to
  present for approval. The seven mesh2d modules total ~5.3 KB of WGSL and need
  `bevy_render::maths::{affine3_to_square, mat2x4_f32_to_mat3x3_unpack}` inlined,
  since Epix has no `maths` module.
- **Pipeline layouts landed (2026-09-17)**: `Mesh2dPipeline`, `create_mesh2d_view_layout`,
  `create_mesh2d_mesh_layout` and `mesh2d_per_object_buffer_batch_size` in
  `mesh2d_pipeline.hpp` plus the new `sprite_render/src/mesh2d_pipeline.cpp`, with
  the `ShaderTypeInfo<Mesh2dUniform>` specialization. The view layout is the
  dynamic view uniform, the globals uniform and the two LUT entries at fragment
  visibility; the mesh layout is one vertex/fragment binding taken from
  `GpuArrayBuffer<Mesh2dUniform>::binding_layout`, and the batch size is reported
  only for the no-storage-buffer fallback, exactly as Bevy's
  `GpuArrayBuffer::<T>::batch_size` does. Tests:
  `tests/header/mesh2d_pipeline_tests.cpp` (2 tests, one of which creates both
  layouts on a real Vulkan device), full suite **83/83** in 47.54 s.
- **Rendering (pipeline layouts)**: the same non-regression sweep - all five
  Mesh2d examples captured through `PrintWindow(PW_CLIENTONLY)`, inspected, one
  distinct cropped scene hash each across three frames, no flagged diagnostics -
  and the capture batch ran with the focus policy intact (first example in
  `keep`, the rest politely).
- **Tooling**: the capture batch's focus restoration is now best-effort - a Win32
  hiccup on a dying previously-focused window aborted two sweeps. The batch
  completes 5/5 with the focus policy intact afterwards.

## Verification gates

- Steps 1-9: focused tests (key bit layout and round trips, pipeline descriptor
  for a specialization key, view/material bind group layouts, cache
  insert/remove/tick behaviour) plus the full build and CTest suite.
- Step 10: a dedicated material2d example, parity tests, full build and CTest,
  and `PrintWindow(PW_CLIENTONLY)` captures of every affected example
  (`batching`, `alpha_mask`, `mesh_rendering`, `primitive_builders`,
  `linear_polygon_primitives`, `mesh_algorithms`, `circular_primitives`,
  `sphere`, `extrusion`, `annulus_rhombus_triangle_capsule`, `primitives_3d`,
  `falling_sand`, `pixel_body_sand`, `liquid_eulerian`, `text_interactive`) with
  the scene compared against the pre-swap capture.
- Every step is a cross-module change (sprite_render plus render/mesh), so each
  batch is presented for approval before committing.

## Dependency status (checked 2026-09-17)

- **`GpuArrayBuffer`** - present (`render/render/include/epix/render/batching.hpp:943,972`, 25 references). Step 2's mesh layout and step 5's batching can use it.
- **`GlobalsUniform`** - present (`render/render/include/epix/render/globals.hpp:21`, 28 references). Step 2's view layout can bind it.
- **`GetBatchData` / `GetFullBatchData`** - present (`batching.hpp`, 69/36 references with existing example specializations), so step 5 plugs into the existing batching machinery rather than inventing one.
- **`Mesh2dUniform`** - absent (0 references); port it in step 2 (`mesh.rs:199-227`).
- **`Material2dBindGroupId`** - absent (0 references); it is defined by the material pipeline, so step 5's `CompareData` is finalized in step 8.
- **`RenderMesh2dInstance`** exists but with Epix's simplified shape (`sprite_render/include/epix/sprite_render/mesh2d.hpp:90`); step 4 replaces its fields with Bevy's (`material_bind_group_id`, `mesh_asset_id`, `transforms`, `tag`, `automatic_batching`).
