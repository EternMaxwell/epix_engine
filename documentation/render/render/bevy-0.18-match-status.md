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

- `PipelineServer` is the intentional `PipelineCache` replacement.  It has
  shared state because it lives in both main and render worlds.  The app
  scheduler guarantees that const and mutable access cannot occur at once;
  mutation is permitted during extraction.  Its default compilation is
  asynchronous; callers may opt into synchronous creation.
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
