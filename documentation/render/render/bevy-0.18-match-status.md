# Bevy 0.18 render/camera match registry

This is the authoritative, tracked registry for matching Epix render and
camera with `bevy_render` and `bevy_camera` 0.18.  It records the original
audit as supplied by the project owner and the project-specific decisions
which qualify that match.

The original IDs are stable: a newly discovered mismatch is appended in a
separate section and must not be numbered as though it were part of R1-R18.
No item is considered complete from a broad module-level claim alone; its
listed interface and implementation behavior must be checked against Bevy.

## Status terms

| Status | Meaning |
| --- | --- |
| FIXED | The relevant Epix interface and behavior now match Bevy, with tests. |
| OPEN | A known mismatch still needs an approved implementation checkpoint. |
| INTENTIONAL | An owner-approved Epix divergence. |
| EXTERNAL | Match is blocked by the current native wgpu API; retain the integration point. |
| DEFERRED | Out of the current feature scope, but must be revisited when its integration point is used. |

## Project decisions that constrain the match

- `PipelineServer` is the intentional `PipelineCache` replacement. It is
  move-only and owns its cache state directly; the main and render worlds each
  receive a separate instance. Its default compilation is asynchronous;
  callers may opt into synchronous creation. The render-world instance also
  exposes Bevy's render-only `block_on_render_pipeline` for preparation paths
  that must wait for a selected render pipeline.
- Slang, rather than WGSL, and direct wgpu resource use are accepted
  divergences.  Direct resource ownership must not conceal unrelated Bevy
  API/behavior mismatches.
- Epix's parallel render-subapp execution is the accepted replacement for
  Bevy pipelined rendering.  It does not automatically satisfy a separate
  Bevy render-graph command-generation API.
- General camera updates belong to Epix's public camera plugin so cameras can
  work without the render module.  Renderer-specific extraction/graph work
  remains in the render module.
- Until wgpu-native accepts Slang-produced SPIR-V without the current bug,
  renderer creation forces the chosen backend to Vulkan in implementation
  code.  Remove only those local workaround lines once the upstream problem
  is fixed; otherwise keep Bevy-shaped settings and behavior.
- `RenderAsset::asset_usage` deliberately has no default.  Other render-asset
  behavior, including fallible preparation/retry and extraction results,
  should match Bevy.
- Bindless support is intentionally absent while the current wgpu-native C
  API lacks the required functionality.
- `ExtractedAsset` compact extraction is an intentional Epix extension.
  There is no `Reextract` reason or extension: re-extracting unchanged input
  has no useful semantic meaning.

## Original R1-R18 audit

### Owner-directed resolution requirements

These requirements are authoritative for the corresponding original IDs,
including where the current native wgpu API lacks an implementation call.

| IDs | Required resolution |
| --- | --- |
| R1 | Current wgpu-native C API has no bindless support, so do not support bindless now. |
| R2-R6 | Match Bevy. |
| R7 | The lack of a default `asset_usage` is intentional. `prepare_asset` must match Bevy. The separated extract API returns `std::expected`; extract and prepare may have different errors, but tagged errors use `std::variant`, never an enum-kind payload. |
| R8 | Match Bevy, updating as necessary for separated extraction and preparation. |
| R9 | Intentional Epix feature; add the missing supporting counterpart where needed. |
| R10 | Match Bevy. |
| R11 | Keep general camera update in the public camera plugin so it works without render. |
| R12 | Match Bevy. |
| R13 | Match Bevy; app/ECS support for fallible system return values is part of the required solution. |
| R14 | Match Bevy. |
| R15 | Match Bevy even though the current wgpu API does not expose the final backend call. |
| R16 | Match Bevy. |
| R17 | Match Bevy. |
| R18 | Not the current main focus, but preserve integration points needed by other work. |

| ID | Area | Status | Current evidence / required next action |
| --- | --- | --- | --- |
| R1 | Bindless | INTENTIONAL | Not supported until the native wgpu C API exposes the needed functionality. |
| R2 | GPU struct layout | FIXED | Layout-aware `ShaderType` serialization replaced raw standard-layout `memcpy`; `RawBufferElementInfo` is explicitly documented as a temporary C++26-reflection workaround. |
| R3 | GPU readback | FIXED | `ReadbackComplete::to_shader_type` uses the shader-layout reader rather than raw `memcpy`, with readback coverage. |
| R4 | Render-graph errors and slots | FIXED | Typed graph run errors, slot/input/subgraph validation, and fallible node execution are preserved through the runner. |
| R5 | View nodes | FIXED | `ViewNodeRunner` owns and updates `QueryState<N::ViewQuery>`, supplies the read-only matching query item, and skips an unset/non-matching view. Automated coverage exercises both branches; the binned-phase renderer window is a direct `ViewNodeRunner` visual example. |
| R6 | Render-graph parallel command work | FIXED | `RenderContext` queues ready buffers and deferred generation tasks. `finish() &&` dispatches generators through `ComputeTaskPool`, restores enqueue order, and submits the completed buffers. Automated coverage proves deferral and parallel execution; RT clear, binned phase, and sorted phase examples have multi-frame GLFW captures. |
| R7 | Render-asset base contract | INTENTIONAL | No-default `asset_usage` is approved.  Fallible preparation/retry behavior is matched and must remain so. |
| R8 | Render-asset extraction semantics | FIXED | Extraction failures log/continue and successfully extracted modified assets are included in `added`. |
| R9 | Compact asset extraction | INTENTIONAL | `ExtractedAsset` extension is retained; `Reextract` support and reason were removed. |
| R10 | Wgpu settings API | FIXED | `WgpuSettings` now includes Bevy-shaped `MemoryHints` and `MemoryBudgetThresholds`, alongside the existing settings fields. Native wgpu v25 exposes neither the device memory-hint nor instance memory-budget descriptor fields; renderer-local comments identify the sole future activation point without inventing a non-Bevy API. |
| R11 | Camera-system ownership | INTENTIONAL | General update remains in `epix::camera` so it works without renderer attachment; render-side work remains renderer-owned. |
| R12 | Camera projection contract | FIXED | `CameraProjection` now requires only Bevy's clip/sub-view matrices, update, far distance, and cascade frustum corners (using Windows-safe `get_far()` spelling). The non-Bevy near getters/setters were removed; built-ins, custom projections, and `Projection` expose Bevy's default frustum construction, and `Projection::is_perspective()` follows Bevy's custom-matrix rule. |
| R13 | Camera update logic | FIXED | `camera_system` is a fallible `std::expected` system over the Bevy-shaped `Projection` wrapper. It consumes creation/resize/DPI events, updates only changed targets/projections/viewports/sub-views, clamps after resolving the target, preserves logical sizing, and rescales viewports on DPI changes. Manual views retain the documented renderer-owned resolution pass. |
| R14 | Manual texture-view helpers | FIXED | `ManualTextureView::with_default_format` now uses Bevy's sRGB RGBA8 default, and `ManualTextureViews` directly exposes its map collection API. Handle ownership remains correctly camera-side and views render-side. |
| R15 | Window maximum frame latency | FIXED | `Window` and `ExtractedWindow` carry Bevy's optional desired frame-latency setting; surface setup uses it or Bevy's default of two through wgpu-native's `SurfaceConfigurationExtras`. As in Bevy, it is extracted when the render-side window is created rather than dynamically reconfiguring an existing surface. |
| R16 | Surface usage | FIXED | Window surfaces no longer unconditionally request `COPY_SRC`; usage follows the Bevy-aligned configuration. |
| R17 | Screenshot integration | FIXED | `ScreenshotPlugin` is render-owned and installed by `WindowRenderPlugin`. The API is Bevy-shaped and component/event based; the retired event/hotkey API is removed. |
| R18 | Render diagnostics | DEFERRED | Render diagnostics/timestamp instrumentation and Tracy/erased-asset diagnostic plugin parity are not the current focus.  Preserve feature/integration boundaries. |

The source audit supplied R1 through R18 only.  There is no original R19;
future findings belong below and receive a separate `N*` identifier.

## Subsequent findings

| ID | Area | Status | Notes |
| --- | --- | --- | --- |
| N1 | Collection/range API parity | FIXED | Bevy iterator-style APIs use C++ lazy ranges or spans/views rather than eager vectors/references where Bevy borrows/slices. |
| N2 | Binned phase retained identity | FIXED | Cached entities and representative pairs preserve `MainEntity`; covered by native visual captures. |
| N3 | Binned `IndexMap` / `RenderBin` removal | FIXED | Shared `utils::IndexMap` implements Bevy-style `swap_remove` / `swap_remove_index`. Bins, batch sets, cached entity keys, and `RenderBin` use it; reverse stale sweeping preserves moved valid entries. Focused utility and binned-phase tests cover moved-entry lookup bookkeeping and stale-entity sweeping. |
| N4 | Mutable extraction parameters | INTENTIONAL | `Extract<ResMut<...>>` remains valid. `Extract` registers only the render-side `ExtractedWorld` proxy: read for read-only parameters and write for mutable ones. Its source parameter state owns a separate main-world change tick/last-run pair, matching Bevy `SystemState` timing without mixing world-local component access ids. |
| N5 | Camera projection plugin | FIXED | `CameraProjectionPlugin` owns `update_frusta`; `CameraPlugin` composes it through `add_plugins`. Transform propagation now uses Bevy's `TransformSystems::Propagate` in `PostUpdate`, and visibility/frustum work is ordered after it. The retained `CameraUpdateSystem` dependency ensures the public Epix camera driver completes before its frustum is read. |
| N6 | Dynamic uniform-buffer writer | FIXED | `DynamicUniformBuffer::get_writer` and its RAII `DynamicUniformBufferWriter` now match Bevy's public writer contract; view-uniform preparation uses it. Since wgpu-native exposes no `write_buffer_with` mapped view, the writer stages encoded bytes and submits one `Queue::writeBuffer` on destruction without changing the public API. A real-device smoke test covers allocation, offsets, upload lifetime, capacity reuse, and bounds; rendering-window captures cover sprite, binned, mesh, and ImGui output across multiple frames. |
| N7 | Render-graph edge-existence contract | FIXED | `EdgeExistence::{Exists, DoesNotExist}` now replaces the ambiguous boolean in `RenderGraph::validate_edge`; all add/remove callers and parity tests use the typed Bevy-equivalent contract. |
| N8 | Sorted render-phase storage | FIXED | `ViewSortedRenderPhases<I>` now owns retained-view keyed `SortedRenderPhase<I>` instances in the render world. Core 2D extraction clears live phases and prunes stale views; queue, batching, and node execution consume the resource rather than per-view phase components. The resource is explicitly move-only, preventing an invalid copy of move-only phases during downstream recompilation. Parity tests cover allocation retention, plugin registration, and this move-only contract; affected examples are visually captured across warmed-up frames. |
| N9 | Bind-group entry construction helpers | FIXED | `BindGroupEntries`/`DynamicBindGroupEntries` provide sequential, explicitly indexed, and single direct-wgpu entries. `BindGroupEntries::buffer_binding`, `texture_binding`, and `sampler_binding` construct raw resource entries without a Rust-style conversion trait; layout counterparts plus `binding_types` cover buffer, texture, sampler, and storage-texture layouts. Fixed helpers store arrays and dynamic helpers expose only spans. Binding arrays remain unavailable with intentional R1 bindless omission. The sorted-phase geometry example uses both helper families. |
| N10 | Prepared bind-group binding resources | FIXED | `BindingResources` is now the ordered, tagged ownership collection shared by non-generic `PreparedBindGroup` and `UnpreparedBindGroup`. Its `std::variant` stores direct wgpu buffers, dimension-tagged texture views, sampler-type-tagged samplers, or `OwnedData`; it exposes spans and materializes direct WebGPU entries in order. `AsBindGroup` now supplies `Data` separately through `bind_group_data` and returns `expected` prepared/unprepared results. The sorted-phase geometry example retains both compute and render bindings through this model. Binding arrays remain intentionally unavailable under R1. |
| N11 | Specialized mesh pipelines | OUT OF SCOPE (MESH) | Mesh does not currently target Bevy parity. This is recorded for boundary clarity only and must not trigger a mesh audit or render/camera work. |
| N12 | Render mesh allocation and morph support | OUT OF SCOPE (MESH) | Mesh does not currently target Bevy parity. This is recorded for boundary clarity only and must not trigger a mesh audit or render/camera work. |
| N13 | Camera visibility change detection | FIXED | `Mut<T>::bypass_change_detection()` is available. `ViewVisibility` scratch reset and continuing-visible updates bypass ticks; hidden-to-visible and visible-to-hidden transitions still mark the component changed. Visibility set ownership/order matches Bevy. |
| N14 | Child-plugin lifecycle registration | FIXED | Every production plugin attaching another plugin now uses `App::add_plugins(...)`, so plugin storage/lifecycle bookkeeping is preserved. This covers camera composition plus render extraction, batching, and view composition. |
| N15 | AsBindGroup layout helper duplication | FIXED | Removed the non-Bevy free layout helpers (`texture_binding`, `sampler_binding`, and peers) and their wrapper entry type from `as_bind_group.hpp`. `AsBindGroup` now returns raw `wgpu::BindGroupLayoutEntry` values built through shared `binding_types`/`BindGroupLayoutEntries`; resource construction remains solely on `BindGroupEntries`. |
| N16 | AsBindGroup error model | FIXED | `AsBindGroupError` is now the typed union `RetryBindGroupNextUpdate`, `CreateBindGroupDirectly`, and `InvalidSamplerType`; diagnostics preserve each cause. The direct-wgpu `GpuAssetCreationError` remains the original no-payload enum as a separate `PrepareAssetError` alternative for nullable resource creation, and normal plus erased asset paths stringify it without conflating it with bind-group errors. |
| N17 | AsBindGroup default preparation | FIXED | Specializations supply only label, layout entries, data, and `unprepared_bind_group`; the C++ default `as_bind_group` helper materializes ordered resources and returns `PreparedBindGroup`. The sorted-phase geometry visual example uses that default path for both compute and render bind groups. |
| N18 | AsBindGroup shader-type conversion | FIXED | `AsBindGroupShaderType<C, T>` now converts a source bind-group value to a `ShaderType` with `const RenderAssets<image::Image>&` access (whose processed value is `GpuImage`). The constrained default uses an ordinary implicit C++ conversion; explicit specializations can consult image metadata. The free `as_bind_group_shader_type<T>(...)` helper provides the C++ call site. Focused tests cover both paths, and the dedicated `as_bind_group_shader_type_image` visual example turns cyan only after a processed image is available, with repeated client-area captures. This is unrelated to unsupported bindless support. |
| N19 | Acceleration-structure binding layouts | OUT OF SCOPE (NATIVE WGPU) | Bevy's `binding_types` also exposes `acceleration_structure()` and `acceleration_structure_vertex_return()`. The standard WebGPU header exposes neither the corresponding bind-group binding type nor resource handle, so this has no current Epix implementation target. This is separate from the intentional R1 bindless omission. |
| N20 | Erased render-asset compact extraction | FIXED (EPIX EXTENSION) | `ErasedRenderAsset` now has `ExtractedAsset` and, when it differs from `SourceAsset`, `extract(source, id, reason, previous_erased_asset) -> expected`. Extraction and deferred preparation store only that payload; byte budgeting also measures it. Epix additionally passes the prior erased GPU object to `prepare_asset`, allowing allocation reuse and incremental updates. This is an intentional extension: Bevy 0.18's erased prepare signature has no previous-GPU parameter. The visual `erased_render_asset_clear` example verifies a compact payload drives the presented color. |
| N21 | Render-phase trait topology | FIXED (INTENTIONAL MINOR API SHAPE) | `PhaseItem` contains only Bevy's common entity/main-entity/draw/range/extra-index contract. Epix intentionally represents mutable access with const/non-const overloads of `batch_range()` and `extra_index()`, rather than Bevy's separate `_mut` methods and paired mutable accessor; the effects are identical. Stored fields are deliberately named differently because C++ cannot share the Rust field/method name. `SortedPhaseItem` alone owns `sort_key()`/`indexed()`. `BinnedPhaseItem` owns key associated types and requires the C++ `create(batch_set_key, bin_key, representative_entity, batch_range, extra_index)` equivalent of Bevy's `new(...)`; the generic renderer no longer supports a no-factory fallback. Core2D's non-Bevy binned/sort shim accessors are removed. Compile-time coverage proves a binned item need not be sortable. |
| N22 | Cached pipeline phase-item name | FIXED | `CachedRenderPipelinePhaseItem` and every generic consumer now use Bevy's `cached_pipeline()` spelling; Core2D, mesh queueing, batching, examples, and tests were migrated. |
| N23 | Default sorted-phase ordering | FIXED | The default path now uses `std::ranges::sort`, the C++ unstable equivalent of Bevy `SortedPhaseItem::sort`; a type may still provide its own static sort policy. |
| N24 | Binned mesh phase selector | FIXED | `binned_render_phase_type_for_mesh(batchable, GpuPreprocessingSupport)` is the C++ namespace-function equivalent of Bevy's inherent enum method (C++ enums cannot own member functions). It selects the same multidrawable, batchable, or unbatchable storage. `GpuPreprocessingSupport` now has its own matching public header, avoiding a batching/binned-phase include cycle. Focused tests cover all three capability paths. |
| N25 | RenderBin entity collection | FIXED | `RenderBin` now owns `utils::IndexMap<MainEntity, InputUniformIndex>` and exposes the borrowed `entities()` collection. Its custom vector/hash-map and the non-Bevy `contains/get/size/iter` façade are removed; binned preparation and tests consume the collection through its lazy range. CPU-prepared batches remain the implementation data required by Epix's CPU fallback. |
| N26 | Unbatchable binned-entity indices | FIXED | `UnbatchableBinnedEntityIndexSet` now has Bevy's `NoEntities`, compact `Sparse` contiguous-index, and `Dense` per-entity fallback behavior. CPU and GPU binned preparation append indices in entity iteration order; rendering reconstructs each one-item phase range from that set. The old prepared `MainEntity` map is removed. Focused tests cover contiguous no-extra indices, consecutive indirect indices, and dynamic-offset demotion; the binned visual example now includes unbatchable meshes in its ordinary GPU-preprocessing scene. |
| N27 | Binned phase encapsulation | OPEN | Bevy keeps `batch_sets`, cached entity keys, validity bits, changed-bin records, and preprocessing mode crate-private. Epix exposes phase internals publicly so batching and tests reach them directly. The needed C++ access surface must preserve Bevy's ownership/visibility boundary. |
| N28 | Binned phase add parameters | OPEN | Bevy `BinnedRenderPhase::add` takes one `(Entity, MainEntity)` representative pair. Epix splits it into two arguments even though all producers already construct both identities together. |

## Completion and verification policy

- Work one approved checkpoint at a time.  Before committing, present the
  concrete changes and verification results for review; commit only after
  approval.  A meaningful checkpoint may be committed before every registry
  row is complete.
- Each new render/camera feature needs automated tests and a proper runnable
  example.  Earlier session features are held to the same standard.
- Visual verification captures the actual rendering window, never the
  terminal.  Capture several frames/runs to check both rendering and
  stability; a green success indicator alone is not evidence of correct
  pixels.
- Rebuild/reconfigure as needed after interface changes.  If CMake download
  or configuration fails transiently, retry it.
- Do not track runtime artifacts such as `imgui.ini`.
