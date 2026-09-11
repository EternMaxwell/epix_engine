# `pbr` module — Bevy 0.18 match tracker

Scope: match `epix::pbr` to Bevy 0.18's `bevy_pbr` for the pieces the audited
modules need. `pbr` is an enabler for `core_graph` rather than one of the four
audited modules: C15's deferred subsystem and C13d both end in `bevy_pbr`'s
`MaterialPlugin`, because in Bevy the *queuing* half of a 3D pass belongs to the
material plugin and not to the core pipeline.

Numbering: `PB<n>` for rows, `PB<n>-D<m>` for divergences.

Status terms: ✅ matches Bevy · ⚠️ intentional divergence, recorded · ❌ missing ·
🚫 not started.

## Findings

| ID | Bevy behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| PB1 | `MeshPipelineKey` (`bevy_pbr/src/render/mesh.rs:2146-2306`): a `u64` key whose low bits are PBR flags and whose high bits are inherited from `bevy_mesh::BaseMeshPipelineKey`; MSAA in the top three bits of the flag region, then blend, tonemapping method, shadow filter method, view projection and screen-space specular transmission as reserved bitfields, with two `const_assert_eq!`s proving no field overlaps a flag or the topology bits | `epix::pbr::MeshPipelineKey` in `pbr/include/epix/pbr/mesh_pipeline_key.hpp` | ✅ | Every flag, bitfield and shift/mask constant is present at Bevy's exact bit positions, `from_msaa_samples`/`msaa_samples` and `from_primitive_topology`/`primitive_topology` round-trip through the inherited base-key bits, and both of Bevy's compile-time overlap assertions are reproduced as test assertions. |
| PB2 | `MeshPipelineViewLayoutKey` (`mesh_view_bindings.rs:79-202`): seven flags selecting one of 128 generated view bind group layouts, with `From<MeshPipelineKey>`, `From<Msaa>`, `From<Option<&ViewPrepassTextures>>` and an order-sensitive `label()` | `epix::pbr::MeshPipelineViewLayoutKey` in the same header | ✅ | All seven flags, `COUNT == 128`, the three conversions and the label all match, including that the deferred lighting-pass-id attachment does not affect the layout. |
| PB3 | The pipeline-key mapping helpers live in `material.rs` but key the *mesh* pipeline: `alpha_mode_pipeline_key` (`:619`), `tonemapping_pipeline_key` (`:635`), `screen_space_specular_transmission_pipeline_key` (`:651`) | `epix::pbr::alpha_mode_pipeline_key`, `tonemapping_pipeline_key`, `screen_space_specular_transmission_pipeline_key` | ✅ | Ported where Bevy keeps them, with Bevy's exact tables including the two that are easy to get wrong: premultiplied and additive share `BLEND_PREMULTIPLIED_ALPHA`, and alpha-to-coverage degrades to `MAY_DISCARD` when MSAA is off. |
| PB4 | `MeshPipeline` (`mesh.rs:1847-1921`) and its `SpecializedMeshPipeline` impl (`:2372-2741`) | none | ❌ | Blocked on its view layouts: Bevy's group 0 has 32 bindings covering lights, point/directional shadow arrays and comparison samplers, the clustered-forward buffers, globals, fog, light probes, visibility ranges, screen-space reflections, SSAO, environment maps, tonemapping LUTs, the prepass textures, the view transmission texture, OIT and the atmosphere. None of those subsystems exist in Epix yet, so a faithful `MeshPipeline` cannot be assembled before the view layout can. `SpecializedMeshPipeline` itself now exists in the render module (render N37). |
| PB5 | `Material`, `MaterialProperties`, `MeshMaterial3d<M>`, `MaterialPlugin<M>`, `MaterialPipeline` and the non-generic `MaterialsPlugin` systems (`material.rs`) | none | ❌ | The whole material facility. Bevy 0.18 type-erases it: `MaterialsPlugin::specialize_material_meshes` and `queue_material_meshes` are **non-generic** and work through `ErasedRenderAssets<PreparedMaterial>` plus `ErasedMaterialPipelineKey`, and material *extraction* is `impl ErasedRenderAsset for MeshMaterial3d<M>` rather than an `extract_materials<M>` system. `MaterialPlugin<M>` itself only registers asset/component plumbing in `PostUpdate`, `RenderStartup` and `ExtractSchedule` and references **no** `RenderSystems` set. `MATERIAL_BIND_GROUP_INDEX == 3`. |
| PB6 | The deferred *queuing* half: opaque materials with `DeferredPrepass` are deliberately never queued into `Opaque3d` and go to `Opaque3dDeferred` via `queue_prepass_material_meshes` instead (`material.rs:1239-1246`, `prepass/mod.rs`) | none | ❌ | This is C15-D1, the only open half of the core_graph deferred row. Until it lands, the deferred G-buffer, lighting-pass id and lighting-id depth textures all hold only their clear values, which is why the `core3d_prepass` example's right half is black by construction. |

## Intentional divergences

| ID | Bevy behavior | Epix adaptation | Rationale |
| --- | --- | --- | --- |
| PB-D1 | `MeshPipelineKey`'s flag values are `bitflags` associated constants, usable directly in `const fn`s | Declared `static const` and defined `inline constexpr` out of class, matching `epix::mesh::BaseMeshPipelineKey::MORPH_TARGETS` | A C++ class cannot hold a `constexpr` member of its own incomplete type. The out-of-class `inline constexpr` definition keeps every value a constant expression, so the conversions and mapping helpers stay `constexpr` exactly as Bevy's `const fn` are. |

## Verification policy

Any `pbr` change that can affect rendering must be verified with a whole-tree
build, the full CTest sweep, and the all-example sweep on both output streams
(`tmp/sweep_examples.ps1`), because `pbr` is below `core_graph`, `sprite_render`
and every 3D example. Value-only changes need the focused tests.
