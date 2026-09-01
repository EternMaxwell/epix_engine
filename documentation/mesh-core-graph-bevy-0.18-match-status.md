# Bevy Mesh and Core Pipeline — Epix Match Status

Reference: Bevy 0.18.0 sources in `tmp/bevy_render-0.18.0/src/mesh` and
`tmp/bevy_core_pipeline-0.18.0/src`, compared with `epix_engine/mesh` and
`epix_engine/render/core_graph` (C++23 no-modules build).

This is the source-of-truth tracker for the mesh/core-graph parity effort.
Every row is retained until it is fixed or the user explicitly accepts it as
intentional, deferred, or out of scope.

## Legend

| Status | Meaning |
| --- | --- |
| ✅ | Functionally equivalent |
| ⚠️ | Intentional divergence; rationale recorded |
| ❌ | Present but behavior or API differs; must fix |
| 🚫 | Missing; must implement |
| REVIEW | Audit incomplete; not yet a parity claim |
| N/A | Non-implementable or out of scope, only with user confirmation |

## Translation Rules

| Bevy | Epix |
| --- | --- |
| `Option<T>` | `std::optional<T>` / optional reference wrapper for borrowing |
| `Result<T, E>` | `std::expected<T, E>` |
| ECS resource and system | Epix resource and scheduled system |
| Rust iterator/slice | C++ range/`std::span` view |
| `wgpu` resource wrapper | Direct `wgpu::` resource where that is an accepted Epix convention |

## Mesh Render Asset and Allocator

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| M1 | `MeshRenderAssetPlugin`: registers render asset + allocator and initializes vertex layouts | `MeshRenderPlugin` | REVIEW | Inspect plugin lifecycle and resources next. |
| M2 | `RenderMesh`: CPU render metadata, vertex-layout reference, indexed buffer metadata | `GPUMesh` | REVIEW | Epix currently owns GPU buffers directly; inspect observable contract. |
| M3 | `RenderMeshBufferInfo` | `GPUMesh` index binding | REVIEW | Need validate count/index format/draw behavior. |
| M4 | `RenderAsset<RenderMesh>`: fallible prepare with previous GPU asset and render-asset usage | `RenderAsset<Mesh>` | REVIEW | Must audit against the completed generic render-asset contract. |
| M5 | `MeshAllocatorPlugin` and `MeshAllocator`: slab allocation, update, free, buffer slices | none | 🚫 | No allocator is present in the mesh module. |
| M6 | `MeshAllocatorSettings` | none | 🚫 | Settings surface is absent with M5. |
| M7 | `MeshBufferSlice`, `SlabId`, allocation query methods | none | 🚫 | Public allocation/query API is absent with M5. |
| M8 | Optional morph target extraction and `MorphPlugin` / `inherit_weights` | none | REVIEW | Determine whether Epix has a morph feature or an explicit unsupported policy. |

## Core Pipeline Plugin and Shared Facilities

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| C1 | `CorePipelinePlugin`: installs 2D, 3D, blit, tonemapping, upscaling, OIT, mip generation, deferred-copy plugins | `CoreGraphPlugin` | ❌ | Epix installs only the 2D graph. All constituent features must be audited individually. |
| C2 | `FullscreenShader`: shared embedded fullscreen-triangle vertex shader and `to_vertex_state()` | `core_graph::FullscreenShader`, initialized by `CoreGraphPlugin` | ✅ | CoreGraphPlugin directly mirrors Bevy CorePipelinePlugin ownership: embeds the shared shader, installs core plugins, then initializes the render-world resource. Core2D only consumes it. Slang remains the accepted shader-language divergence. |
| C3 | `FullscreenMaterial<T>` and `FullscreenMaterialPlugin<T>`: extracted uniform component, two HDR/non-HDR pipelines, a typed view node, and explicit graph edges | none | 🚫 | Epix has the generic extraction, uniform, view-node, and A/B post-process primitives, but no corresponding generic core-pipeline facility. |
| C4 | General `BlitPlugin`, `BlitPipeline`, and `BlitPipelineKey` | `core_graph::BlitPlugin`, `BlitPipeline`, and `BlitPipelineKey` | ✅ | Reusable core-graph facility now owns an embedded fragment shader, render-startup resource initialization, specialized-pipeline cache, direct-WGPU bind-group creation, and format/blend/sample specialization. Core2D's private output blit remains separately audited under C17. |
| C4a | `SpecializedRenderPipeline` / `SpecializedComputePipeline` specialize from `&self` | `render::SpecializedRenderPipeline` / `render::SpecializedComputePipeline` concepts | ✅ | Pipeline instances own `Key` and `specialize(key)`, and cache methods receive the specialization instance exactly as in Bevy. |

## Core 2D Pipeline

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| C5 | `Core2dPlugin` required components, phase extraction, depth preparation, graph registration | `Core2dPlugin` | REVIEW | Audit lifecycle and schedule in `src/core2d.cpp`. |
| C6 | `core_2d::graph::Core2d`, input slot, and all graph node labels | `Core2dGraph`, `Core2dNodes` | ❌ | Epix labels omit Bevy graph input and several post-processing/MSAA labels. |
| C7 | Binned `Opaque2d`, `AlphaMask2d`, their bin keys, and batch-set key | `Opaque2D` | ❌ | Epix only exposes a sorted-style opaque phase; alpha-mask and Bevy binned phase contracts are absent. |
| C8 | Sorted `Transparent2d` phase item | `Transparent2D` | REVIEW | Compare fields, range API, sorting, and index semantics. |
| C9 | `extract_core_2d_camera_phases` maintains opaque, alpha-mask, transparent phase resources | `Core2dPlugin` setup | REVIEW | Inspect implementation and phase lifetime. |
| C10 | `prepare_core_2d_depth_textures` cached depth textures per target and MSAA samples | `Node2D` / view resources | REVIEW | Need verify resource preparation and no-view behavior. |
| C11 | `MainOpaquePass2dNode` with independent opaque + alpha-mask pass work | `Node2D<Opaque2D>` | ❌ | Current node has no alpha-mask phase and issues immediate work. |
| C12 | `MainTransparentPass2dNode` clears/render transparent pass independently | `Node2D<Transparent2D>` | ❌ | Current generic node shares one implementation and does not match Bevy pass separation. |

## Core 3D and Pipeline Stages

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| C13 | `Core3dPlugin`, 3D graph, opaque/alpha-mask/transmissive/transparent phases, depth/transmission resources | none | 🚫 | Entire Bevy core 3D pipeline absent from `core_graph`. |
| C14 | Prepass components, textures, nodes, and preparation | none | 🚫 | No core-graph prepass implementation. |
| C15 | Deferred phases, nodes, and lighting-id copy plugin | none | 🚫 | No core-graph deferred implementation. |
| C16 | Tonemapping settings, LUTs, pipelines, and node | none | 🚫 | No tonemapping subsystem. |
| C17 | Upscaling plugin, per-view pipeline, and node | `Core2dBlitNode` | REVIEW | Output blit is related but must be checked against Bevy’s dedicated upscaling contract. |
| C18 | OIT settings, buffers, resolve pipeline/node | none | 🚫 | No OIT subsystem. |
| C19 | Experimental depth mip-generation plugin and resources | none | 🚫 | No depth-pyramid/mip-generation subsystem. |
| C20 | Skybox pipeline and prepass integration | none | 🚫 | No core-pipeline skybox subsystem. |

## Audit Log

| Date | Evidence | Result |
| --- | --- | --- |
| 2026-08-31 | Read all source-file inventories, `bevy_render::mesh`, `bevy_core_pipeline::lib`, and `core_2d` public declarations directly from local Bevy 0.18.0 source. | Initial numbered inventory created. |
| 2026-08-31 | C2: full no-modules reconfigure/rebuild; `tests_header_render_render_parity_tests` and `tests_header_mesh_module_test`; three actual GLFW client-area captures of `examples_header_mesh_rendering`. | Shared `FullscreenShader` fixed and visually verified. |
| 2026-08-31 | C2 ownership and shader-coordinate correction: targeted no-modules rebuild, the same two focused tests, and three fresh GLFW client-area captures of `examples_header_mesh_rendering`. | `CoreGraphPlugin` owns initialization exactly as Bevy `CorePipelinePlugin`; fullscreen-triangle UV and clip-space mapping match Bevy. |
| 2026-09-01 | C3/C4 source audit against Bevy `fullscreen_material.rs` and `blit/mod.rs`; C4a targeted no-modules build and `tests_header_render_render_parity_tests`. | Documented the absent generic facilities and corrected the shared specialized-pipeline instance contract. |
| 2026-09-01 | C4: read Bevy `blit/mod.rs` and `blit.wgsl` directly; implemented the equivalent core-graph plugin, pipeline, key, bind group, and specialization contract. Reconfigured the no-modules build, ran `tests_header_render_render_parity_tests`, and built `examples_header_render_glfw_blit_pipeline`. | The focused test passed. Three actual GLFW client-area captures used a red camera/output fallback and blue intermediate source; all were stable at 960x540 with identical center BGR `(246,173,56)`. The visible blue therefore proves that BlitPipeline overwrote the red output fallback. |
