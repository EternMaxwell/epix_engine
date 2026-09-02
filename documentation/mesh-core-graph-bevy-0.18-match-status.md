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
| C1 | `CorePipelinePlugin`: installs 2D, 3D, blit, tonemapping, upscaling, OIT, mip generation, deferred-copy plugins | `CoreGraphPlugin` | ❌ | Epix now installs Core2D, the general blit facility, and upscaling. Core3D, tonemapping, OIT, mip generation, and deferred-copy remain absent or separately incomplete. |
| C2 | `FullscreenShader`: shared embedded fullscreen-triangle vertex shader and `to_vertex_state()` | `core_graph::FullscreenShader`, initialized by `CoreGraphPlugin` | ✅ | CoreGraphPlugin directly mirrors Bevy CorePipelinePlugin ownership: embeds the shared shader, installs core plugins, then initializes the render-world resource. Core2D only consumes it. Slang remains the accepted shader-language divergence. |
| C3 | `FullscreenMaterial<T>` and `FullscreenMaterialPlugin<T>`: extracted uniform component, two HDR/non-HDR pipelines, a typed view node, and explicit graph edges | none | 🚫 | Epix has the generic extraction, uniform, view-node, and A/B post-process primitives, but no corresponding generic core-pipeline facility. |
| C4 | General `BlitPlugin`, `BlitPipeline`, and `BlitPipelineKey` | `core_graph::BlitPlugin`, `BlitPipeline`, and `BlitPipelineKey` | ✅ | Reusable core-graph facility now owns an embedded fragment shader, render-startup resource initialization, specialized-pipeline cache, direct-WGPU bind-group creation, and format/blend/sample specialization. Core2D's private output blit remains separately audited under C17. |
| C4a | `SpecializedRenderPipeline` / `SpecializedComputePipeline` specialize from `&self` | `render::SpecializedRenderPipeline` / `render::SpecializedComputePipeline` concepts | ✅ | Pipeline instances own `Key` and `specialize(key)`, and cache methods receive the specialization instance exactly as in Bevy. |

## Core 2D Pipeline

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| C5 | `Core2dPlugin` required components, phase extraction, depth preparation, graph registration | `Core2dPlugin` | ✅ | The plugin now adds Bevy's `DebandDither::Disabled`, `CameraRenderGraph{Core2d}`, and `Tonemapping::None` requirements to `Camera2d`, and registers `ExtractComponentPlugin<Camera2d>`. Phase/depth/graph details remain separately tracked under C6–C12 and C17; the actual tonemapping pipeline is C16. |
| C6 | `core_2d::graph::Core2d` and all graph node labels | `Core2dGraph`, `Core2dNodes` | ✅ | `Core2dNodes` now exposes Bevy's complete public label set. Core2D installs exactly Bevy's eight default nodes and chain: main passes, post-processing endpoints, tonemapping integration point, and upscaling. MSAA writeback, wireframe, bloom, general post-processing, FXAA, SMAA, and CAS remain labels for their owning optional plugins. The actual tonemapping node implementation is C16. |
| C7 | Binned `Opaque2d`, `AlphaMask2d`, their bin keys, and batch-set key | Binned `Opaque2D` / `AlphaMask2D`, `BatchSetKey2D`, and mesh producer | ✅ | Core2D now uses two Bevy-shaped binned phase types. Mesh extraction writes `RenderMesh2dInstances`, keyed by `MainEntity` exactly as Bevy, so placeholder binned render entities never become an implicit data dependency. The C++ `MeshAlphaMode2d` tagged union covers `Opaque`, `Mask(cutoff)`, and `Blend`; mask pipelines discard below the cutoff and retain depth writes. |
| C8 | Sorted `Transparent2d` phase item | `Transparent2D` | ✅ | Entity, main entity, cached pipeline, draw function, range, extra index, reverse-depth sorting, and the `indexed` discriminator now correspond. Mesh queueing reads `GPUMesh::is_indexed`; sprite and text producers explicitly report non-indexed geometry. |
| C9 | `extract_core_2d_camera_phases` maintains opaque, alpha-mask, transparent phase resources | `extract_core2d_camera_phases` | ✅ | Each retained Core2D view now clears transparent items, prepares both binned phases with `GpuPreprocessingMode::None`, and removes dead view entries. |
| C10 | `prepare_core_2d_depth_textures` cached depth textures per target and MSAA samples | General `view::create_view_depth` | ❌ | Texture format, reverse-Z clear value, target size, and sample count match. However, Epix allocates depth for every extracted view in `ViewPlugin::ManageViews`; Bevy owns the Core2D eligibility check and cached allocation in `Core2dPlugin::PrepareResources`, only after live Core2D opaque/transparent phases exist. |
| C11 | `MainOpaquePass2dNode` with independent opaque + alpha-mask pass work | `MainOpaquePass2DNode` | ✅ | The dedicated typed view node resolves both binned phases and records them, in order, into one deferred generated command buffer with Bevy-matching color/depth attachments and camera viewport. |
| C12 | `MainTransparentPass2dNode` clears/render transparent pass independently | `MainTransparentPass2DNode` | ✅ | A separate typed view node records the retained sorted transparent phase into its own deferred generated command buffer. WebGL-only viewport reset remains irrelevant to the forced Vulkan path. |

## Core 3D and Pipeline Stages

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| C13 | `Core3dPlugin`, 3D graph, opaque/alpha-mask/transmissive/transparent phases, depth/transmission resources | none | 🚫 | Entire Bevy core 3D pipeline absent from `core_graph`. |
| C14 | Prepass components, textures, nodes, and preparation | none | 🚫 | No core-graph prepass implementation. |
| C15 | Deferred phases, nodes, and lighting-id copy plugin | none | 🚫 | No core-graph deferred implementation. |
| C16 | Tonemapping settings, LUTs, pipelines, and node | none | 🚫 | No tonemapping subsystem. |
| C17 | Upscaling plugin, per-view pipeline, and node | `UpscalingPlugin`, `ViewUpscalingPipeline`, `UpscalingNode` | ✅ | Top-level plugin specializes the shared `BlitPipeline` per view during `Prepare`, stores the selected ID on the view, and provides a typed deferred-command node under `Core2dNodes::Upscaling`. It preserves Bevy clear color, camera-output mode, later-camera alpha blend, viewport scissor, and source-view bind-group caching. The render-world, move-only `PipelineServer` owns its PipelineCache-equivalent state directly and uses Bevy's `block_on_render_pipeline` before the node runs; the main world has a separate server for explicit main-world GPU work. Direct wgpu texture-view identity is the accepted representation difference. |
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
| 2026-09-01 | C5: read Bevy `core_2d/mod.rs`, `main_opaque_pass_2d_node.rs`, and `main_transparent_pass_2d_node.rs` against Epix `core2d.cpp` and `core2d.hpp`; implemented the missing Core2D camera requirements and `Camera2d` extraction. | `tests_header_render_render_parity_tests` passed with a focused test that verifies the main-world defaults and the render-world marker after the deliberate ExtractCommands deferred-application stage. A rebuilt `examples_header_mesh_rendering` produced three actual GLFW client-area captures at 1280x720; their scene-only hashes were identical and showed the expected opaque, gradient, and transparent geometry. Graph, phase, depth, and upscaling sub-mismatches remain separately numbered. |
| 2026-09-01 | C6: read Bevy `core_2d::graph`, camera-driver graph invocation, and Epix graph runner/context plus `Core2dGraph`. | Confirmed that the view entity is graph-context state in both engines, not a Core2D input slot; this established the public-label/default-chain work completed below. |
| 2026-09-01 | C6: matched Bevy's `Node2d` public label set and the default Core2D graph node chain, while retaining optional-plugin node ownership. | The focused `Core2dGraph.MatchesBevyDefaultNodesAndChain` test verifies all default nodes, all extension-only absences, and every default edge; `tests_header_render_render_parity_tests` also passed. Rebuilt `examples_header_mesh_rendering` produced three GLFW client captures at 1280x720 with identical scene-only pixels below the dynamic overlay. C16 still replaces the tone-mapping placeholder with its GPU node. |
| 2026-09-01 | C7-C10: read Bevy `core_2d/mod.rs` and both Core2D pass nodes against Epix `core2d.hpp/.cpp`, `render_phase.hpp`, `binned_phase.hpp`, `batching.hpp`, `view.cpp`, and mesh queueing. | Epix's generic binned-phase support is already suitable; the remaining work is Core2D/mesh adoption, an indexed transparent field, Core2D-owned phase lifetime, and moving depth preparation to the matching plugin/schedule. |
| 2026-09-01 | C8: added Bevy `Transparent2d::indexed` parity to the shared Core2D sorted phase. | Mesh queues its actual `GPUMesh::is_indexed()` value; sprite and text queue their non-indexed geometry explicitly. The focused phase-item test and `tests_header_render_render_parity_tests` pass. Mesh rendering/batching, sprite basic/pressure, and text font-image/interactive all rebuilt and each produced three actual GLFW client captures with identical scene-only pixels beneath the dynamic overlay. |
| 2026-09-01 | C11-C12/C17: read Bevy Core2D opaque/transparent nodes and `upscaling/mod.rs` plus `upscaling/node.rs` against Epix `Node2D` and `Core2dBlitNode`; confirmed Epix `RenderContext::add_command_buffer_generation_task` and its focused parallel-execution test. | Pass attachment behavior is broadly correct, but the phase/node split is not. Core2D nodes should use the already available deferred command-buffer-generation path. Core2D-private output blitting must become the general-blit-based `UpscalingPlugin` path, including per-view prepared pipeline selection and its required pipeline wait. |
| 2026-09-01 | C17: implemented `UpscalingPlugin`, `ViewUpscalingPipeline`, and typed `UpscalingNode`; removed Core2D-private shader/pipeline/UI ownership. Full no-modules reconfigure/rebuild, `tests_header_render_render_parity_tests`, and three GLFW client-area captures of `examples_header_mesh_rendering`. | Focused tests passed. The three 1280x720 render-window captures had identical SHA-256 `33D7160E8843C124FF1E7F7B89B987646493D86F513C7663C9864C7B85404D4D` and center ARGB `-3684957`; inspected capture showed the expected opaque, transparent, and textured meshes. PipelineServer was subsequently made move-only and world-local, restoring Bevy's `block_on_render_pipeline` for the render-world upscaling preparation path. |
| 2026-09-01 | C17 follow-up: removed `PipelineServerData` indirection; inserted independent move-only PipelineServer resources in the main and render worlds; restored only `block_on_render_pipeline`; made `waiting_pipelines()` a non-owning range. Performed a no-modules CMake reconfigure, Ninja clean (1,285 outputs), and rebuild at `-j10`. | The freshly rebuilt `tests_header_render_render_parity_tests` passed. Three `PrintWindow` captures targeted the actual `GLFW30` “Mesh Rendering Visual Test” window rather than its console; all showed the expected 2D mesh scene. Frame hashes differ only because the live NVIDIA frame-rate overlay changes, not because the scene is white or missing. |
| 2026-09-01 | C17 parity correction: compared `prepare_view_upscaling_pipelines` directly with Bevy 0.18. | Epix now uses the same per-prepare output-texture set to select later-camera alpha blending, and does not omit `Skip` views from pipeline preparation. `specialize`, `block_on_render_pipeline`, and the per-view component insertion deliberately run every Prepare frame in both engines; specialization only queues a pipeline on a cache miss. `tests_header_render_render_parity_tests` passed. |
| 2026-09-01 | C17 final verification: rebuilt `examples_header_mesh_rendering` and `examples_header_render_glfw_blit_pipeline` at `-j10`. | `PrintWindow(PW_CLIENTONLY)` captured the actual `GLFW30` client windows, never their consoles. The mesh example visibly contained its opaque square, both textured gradients, and overlapping transparent circle/rectangle; scene-only frames 2–3 had identical SHA-256 `26CDDE02B781E6AD2F3F65996BDBDF2EF2BC0C63BCBCCA6FA6B25CBF6AC05E77`. The blit example was uniformly blue, proving the source overwrote its red output fallback; scene-only frames 2–3 had identical SHA-256 `38EEAC9AD5F9D726F0A66C1BB84651B990062BB1B4E75507B3BA9462B696F2F6`. The uncropped first frames and all overlay regions vary only because of the live NVIDIA performance overlay. |
| 2026-09-01 | C13: read Bevy `core_3d/mod.rs` and its graph/plugin registration against all Epix core-graph sources. | Core3D has no Epix counterpart: Bevy's 3D graph labels, Camera3D lifecycle, opaque/alpha-mask binned phases, transmissive/transparent sorted phases, depth/transmission preparation, and main-pass nodes are all still missing. |
| 2026-09-02 | C7/C9/C11/C12: compared Bevy 0.18 `core_2d/mod.rs`, `main_opaque_pass_2d_node.rs`, `main_transparent_pass_2d_node.rs`, and `bevy_sprite_render::mesh2d::mesh.rs` with Epix Core2D and mesh producers. Built the no-modules targets at `-j10`; ran 5 mesh tests and 13 Core2D/binned tests. | Added binned opaque and alpha-mask phases, main-entity keyed mesh instances, tagged alpha modes, and the two dedicated deferred pass nodes. A Slang reflection check verified the 96-byte structured-buffer stride; the C++ element now has matching tail padding. Three `PrintWindow(PW_CLIENTONLY)` captures of the actual `GLFW30` alpha-mask window have identical scene-only SHA-256 `25A07F31885DFA24F433F38C640BA409D1A5915BD439AFC390C7F62F6F5677D5` below the dynamic overlay and visibly show the cyan/orange alpha checker. |
