# Bevy 0.18 Sprite and Sprite-Render — Numbered Mismatch Tracker

Audit scope: `epix_engine/sprite` and `epix_engine/sprite_render` against pinned
Bevy v0.18.0 commit `5f8270f2e049f90139a503d1e930070d926f9427`.

| Bevy source | Epix counterpart |
| --- | --- |
| `crates/bevy_sprite/src/{lib.rs,sprite.rs,texture_slice/**}` | `epix_engine/sprite` |
| `crates/bevy_sprite_render/src/lib.rs` | `epix_engine/sprite_render/src/render.cpp` plugin composition |
| `crates/bevy_sprite_render/src/render/{mod.rs,sprite.wgsl,sprite_view_bindings.wgsl}` | `epix_engine/sprite_render/{include/epix/sprite_render/render.hpp,src/render.cpp}` |
| `crates/bevy_sprite_render/src/mesh2d/**` | `epix_engine/sprite_render/{include/epix/sprite_render/mesh2d.hpp,src/mesh2d.cpp}` |
| `crates/bevy_sprite_render/src/{text2d,texture_slice,tilemap_chunk}/**` | Epix text, texture-slice, and currently absent tilemap paths |

This tracker refines S1-S16 in
`documentation/mesh-core-graph-bevy-0.18-match-status.md`, which remains the
module-wide source of truth. Rows stay open until implemented and verified or
explicitly accepted as intentional, deferred, or out of scope.

Status legend: ✅ aligned · ❌ mismatch to fix · ⚠️ intentional divergence ·
🚫 missing/deferred · REVIEW · N/A.

## S12: sprite extraction, queueing, batching, and draw commands

| ID | Pinned Bevy behavior | Current Epix behavior | Status | Required alignment |
| --- | --- | --- | --- | --- |
| S12a | `ExtractedSprites` and `ExtractedSlices` are render-world resources cleared and rebuilt each extract; every `ExtractedSprite` stores both main and synced render entity identity | Slices are a resource, but each sprite is spawned as a `TemporaryRenderEntity` carrying `ExtractedSprite` and `SpriteBatch`; the already-available `RenderEntity` mapping is not used | ❌ | Add the `ExtractedSprites` resource, query `RenderEntity` by value, retain both identities in each record, clear/rebuild both vectors, and stop spawning temporary sprite entities. |
| S12b | `SpriteAssetEvents` copies every image event during extraction; persistent `ImageBindGroups` drops cached entries for `Unused`, `Modified`, and `Removed`, while `Added` and `LoadedWithDependencies` retain/create lazily | No render-world event resource exists; texture bind groups are rebuilt in a function-local map every prepare pass | ❌ | Add both public resources, extract all event alternatives, and invalidate exactly the Bevy alternatives before lazy bind-group creation. |
| S12c | `queue_sprites` specializes one pipeline per view key, builds a local set from `RenderVisibleEntities::iter<Sprite>()`, queues resource-backed extracted sprites by main identity, and sets `extracted_index` plus `indexed = true`; GPU-image readiness is not checked here | Queueing queries temporary sprite components, repeats a `RenderLayers` test, checks `RenderAssets<Image>` early, has no extracted index, and marks phase items non-indexed | ❌ | Queue from `ExtractedSprites`, use the already-computed visible-entity list as the sole camera visibility gate, defer missing-image handling to preparation, preserve extracted index, and mark sprite items indexed. |
| S12d | `SpriteMeta` owns `RawBufferVec<u32>` index data and an 80-byte vertex-rate `RawBufferVec<SpriteInstance>`; `SpriteBatches` is keyed by `(RetainedViewEntity, batch entity)` and stores image ID plus full instance range | `SpriteGeometryBuffers` owns two quad vertex buffers and a `uint16` index buffer; `SpriteInstanceBuffer` is a storage buffer; `SpriteBatch` is a component holding a bind group and only a starting index | ❌ | Replace the custom geometry/storage path with Bevy-shaped index and instance vertex buffers and a per-view batch resource. Keep buffer growth/upload as the C++ `RawBufferVec` adaptation. |
| S12e | `prepare_sprite_image_bind_groups` walks each sorted transparent phase, treats non-sprite items as batch barriers, batches adjacent equal image IDs, skips missing GPU images, expands sliced sprites, writes phase-head batch ranges, uploads once, and initializes indices `2,0,1,1,3,2` as `u32` | The broad walk and adjacent-image behavior are similar, but they target component batches, local bind groups, storage instances, and the separate quad geometry; a component can be overwritten while preparing another view | ❌ | Preserve the existing correct UV/size/slice arithmetic while moving ownership, ranges, cache behavior, index type/order, and upload timing to the Bevy model. Add a two-view regression. |
| S12f | `SetSpriteViewBindGroup<0>` binds the sprite-specific view group with `ViewUniformOffset`; `SetSpriteTextureBindGroup<1>` and `DrawSpriteBatch` look up `(retained view, item entity)`, skip a missing batch, bind the cached image group, bind `u32` indices plus the instance vertex buffer, and draw `0..6` over the batch range | Commands bind generic view group 0, storage instances at group 1, a component texture group at group 2, two explicit quad vertex buffers, and `uint16` indices; missing data is reported as command failure rather than Bevy `Skip` | ❌ | Port the three Bevy command contracts and their skip behavior, using the resource-owned batch and bind-group maps. |
| S12g | Plugin initialization creates `ImageBindGroups`, specialized pipelines, sprite metadata, both extracted resources, event storage, and batches; extraction orders `extract_sprites` in `SpriteSystems::ExtractSprites`; render systems run queue in `Queue`, both preparation systems in `PrepareBindGroups`, and binned Core2D sorting in `PhaseSort` | Only slices, custom buffers/cache, and a draw ID are initialized; extraction has no set label; queue is in `Queue`, but batch preparation is in `PrepareResources`; there is no view-bind preparation, image-event extraction, or explicit Core2D binned sorting here | ❌ | Match resource initialization and scheduling exactly. Epix must explicitly configure any set label its scheduler cannot infer, as with M8c. |
| S12h | `compute_slices_on_asset_event` runs before Bevy `AssetEventSystems`; changed sprite slices are recomputed in the same `ComputeSlices` set | Both systems use `ComputeSlices`, but image-event recomputation has no ordering against Epix `AssetSystems::WriteEvents` | ❌ | Order the event consumer before the corresponding event flush, using Epix `AssetSystems::WriteEvents` as the existing C++ label adaptation. |
| S12i | `apply_scaling` performs Bevy float arithmetic directly, including its IEEE infinity/NaN behavior for zero texture or quad dimensions | Epix returns early when any relevant dimension is zero | ❌ | Remove the invented zero guard and add direct finite and zero-dimension oracle coverage. |
| S12x | Bevy has no `CustomRendered` exclusion in sprite extraction | Epix excludes entities carrying the renderer-wide `CustomRendered` marker | REVIEW | Audit all three current consumers before deciding whether this is an accepted Epix opt-out extension or a behavior-changing mismatch to remove. Do not silently preserve it as parity. |

## S16: sprite pipeline and shader-visible contract

| ID | Pinned Bevy behavior | Current Epix behavior | Status | Required alignment |
| --- | --- | --- | --- | --- |
| S16a | Public `SpritePipelineKey` encodes HDR, in-shader tonemapping, deband dithering, the eight tonemapping methods, and MSAA in the high three bits; `from_msaa_samples`, `msaa_samples`, and `from_hdr` are part of the API | Private cache key is only `(target format, sample count)` | ❌ | Port the public bit key and specialize through existing `SpecializedRenderPipelines<SpritePipeline>`. Add exact bit and view-key tests. |
| S16b | `SpritePipeline` owns descriptor-backed view and material layouts plus one shader handle. Group 0 contains dynamic `ViewUniform` and LUT texture/sampler; group 1 contains texture then filtering sampler | Private cache owns concrete generic-view, storage-instance, and sampler-then-texture layouts plus separate vertex/fragment shader handles | ❌ | Create the two Bevy-shaped layouts in render startup, including the existing CoreGraph tonemapping LUT helpers, and use texture binding 0 / sampler binding 1. |
| S16c | The sole 80-byte instance vertex layout is three transposed affine columns, linear color, and UV offset/scale at locations 0-4. The shader derives quad position and UV from `vertex_index`; no quad vertex buffer or position-size payload exists | A 112-byte storage record contains a full `mat4`, UV, color, and position offset/scale; separate position and UV vertex buffers provide locations 0-1 | ❌ | Bake size/anchor/slice transforms into a 3x4 transposed affine record, switch to one instance-rate vertex layout, and derive corners from index bits. Assert size, offsets, transpose, and index sequence. |
| S16d | LDR views optionally tonemap in the sprite fragment shader using view color grading and the selected LUT; debanding is emitted only with in-shader tonemapping. HDR views skip in-shader tonemapping. | Fragment shader only multiplies sampled texture by tint; no LUT, color grading, method key, or dithering path exists | ❌ | Port Bevy tonemapping/deband branches into the accepted Slang shader language and verify method selection on a real LDR view. |
| S16e | Specialization chooses HDR format or Bevy-default sRGB, alpha blending, Core2D depth format with no writes and `GreaterEqual`, key-derived MSAA, and no extra geometry buffers | Color format is passed through from `ViewTarget`, blending/depth compare are equivalent, depth format is hard-coded to the same current value, and MSAA is passed separately; geometry/storage layouts differ | ❌ | Use the key-derived target/depth/multisample descriptor and assert the complete descriptor. Keep Slang and direct wgpu descriptor spelling as documented representation adaptations only. |

## Deferred neighboring rows

| ID | Scope | Status | Dependency |
| --- | --- | --- | --- |
| S7/S15 | Text2d ownership and `extract_text2d_sprite` integration | REVIEW | Audit after S12 establishes the shared extracted resource/batch model; the current separate text mesh renderer must not be conflated with sprite parity. |
| S9/S10 | Mesh2d and generic Material2d pipeline | REVIEW | Existing decomposition remains valid. S12/S16 can use already-present render pipeline, uniform, view, LUT, phase, and asset infrastructure without first landing Material2d. |
| S11 | Wireframe2d | 🚫 | Requires the S9/S10 Mesh2d/Material2d pipeline. |
| S14 | Tilemap chunk and material | 🚫 | Requires the S10 generic Material2d pipeline. |

## Audit log

| Date | Evidence | Result |
| --- | --- | --- |
| 2026-09-15 | Read pinned Bevy `bevy_sprite_render/src/lib.rs`, all 1,011 lines of `render/mod.rs`, `render/sprite.wgsl`, and `render/sprite_view_bindings.wgsl` against Epix `sprite_render/render.hpp` and `src/render.cpp`; checked existing render support for synced `RenderEntity` queries, `RenderVisibleEntities`, `SpecializedRenderPipelines`, image asset events, dynamic view-uniform offsets, and CoreGraph LUT helpers. | Split broad rows S12/S16 into independently testable S12a-S12i/S12x and S16a-S16e. The mismatches are real architecture and behavior gaps, not naming-only differences. No base-render change is currently required: the needed generic infrastructure already exists. |
