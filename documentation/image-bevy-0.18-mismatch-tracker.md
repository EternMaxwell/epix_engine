# Bevy 0.18 Image Module — Numbered Mismatch Tracker

Audit scope: `epix_engine/image` against Bevy `v0.18.0` (commit
`5f8270f2e049f90139a503d1e930070d926f9427`) sources in `tmp/bevy-0.18.0`:

| Bevy source | Epix counterpart |
| --- | --- |
| `crates/bevy_image/src/image.rs` | `epix_engine/image/include/epix/image/image.hpp`, `src/image.cpp` |
| `crates/bevy_image/src/image_loader.rs` | `epix_engine/image/include/epix/image/image_loader.hpp`, `src/image_loader.cpp` |
| `crates/bevy_image/src/texture_atlas.rs` | `epix_engine/image/include/epix/image/texture_atlas.hpp` |

This tracker refines the `I1`–`I13` rows of
`documentation/mesh-core-graph-bevy-0.18-match-status.md`, which remains the
module-wide source of truth. Every row is retained until it is fixed or the
user explicitly accepts it as intentional, deferred, or out of scope.

Status legend: ✅ aligned · ❌ mismatch to fix · ⚠️ intentional divergence ·
🚫 missing (deferred) · REVIEW · N/A.

## Batch: I1 `Image` data contract

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| G1 | `Image::texture_descriptor: TextureDescriptor<Option<&'static str>, &'static [TextureFormat]>` is the single source of truth for `size.width`, `size.height`, `size.depth_or_array_layers`, `dimension`, and `format` | `m_width`, `m_height`, `m_depth_or_layers`, `ImageType m_type`, `Format m_format` as five separate intrinsics | ❌ | Mismatch `I1`. The GPU-facing description of the image lives outside the image and is reconstructed by the render module at prepare time. Fix: add the descriptor to `Image` and derive the dimension/format/accessor behaviour from it. |
| G2 | `RenderAssetUsages` for `Image::asset_usage` | module-local `ImageUsage` enum (`Main`/`Render`/`Both`) | ❌ | Mismatch `I1`/`M4`. `Mesh` already uses `assets::RenderAssetUsages` (M4), so image is the inconsistent one. Fix: replace `ImageUsage` with `assets::RenderAssetUsages`. |
| G3 | `Image::data: Option<Vec<u8>>` | `std::vector<std::byte> data` plus `bool m_has_data` | ⚠️ | The present/absent distinction is already preserved and was independently verified under `P5`, so the observable contract matches. A `std::optional<std::vector<std::byte>>` would match the type shape exactly; recorded as a representational divergence rather than a behaviour mismatch. |
| G4 | `Image::data_order: TextureDataOrder` | none | 🚫 | Needed for layered/mipped uploads. Part of `I1`. |
| G5 | `Image::texture_view_descriptor: Option<TextureViewDescriptor<Option<&'static str>>>` | none | 🚫 | Part of `I1`. |
| G6 | `Image::copy_on_resize: bool` | `m_copy_on_resize` | ✅ | Field exists with the Bevy default of `false`. |
| G7 | The render module consumes the image's declared texture descriptor rather than reconstructing it | `render/src/image.cpp` `format_cast(image::Format)` and `dimension_cast(image::ImageType)` rebuild the wgpu format and dimension at prepare time | ✅ | Mismatch `I1` + parent-module note. Bevy's render preparation reads `image.texture_descriptor` directly, so the mapping belongs to the image module. Fix: the mapping moved into the image module (`texture_format_cast`/`dimension_cast` in `image.cpp`) and `RenderAsset<Image>::prepare_asset` now copies `asset.texture_descriptor`; the render-side `format_cast` was deleted. This was a *consequence* of the image-side fix, not an independent render defect. |
| G8 | `Image::sampler: ImageSampler` where `ImageSampler` is **Bevy's own** enum `Default | Descriptor(ImageSamplerDescriptor)`, with `linear()`, `nearest()`, and `get_or_init_descriptor()` | bare `enum class ImageSampler { Default, Descriptor }` with the descriptor stored as a **separate `Image` member** (`m_sampler_descriptor`) | ✅ | Mismatch `I4`. Bevy's variant carries the payload, so only images that opt into a custom sampler pay for one and a descriptor cannot exist without the `Descriptor` variant. Epix's split representation lets the two disagree. Fix: make `ImageSampler` a payload-carrying variant and delete `m_sampler_descriptor`, adding `get_or_init_descriptor()`. |
| G9 | `ImageSamplerDescriptor` is **Bevy's own** struct over Bevy's `ImageAddressMode`/`ImageFilterMode`/`ImageSamplerBorderColor` (not wgpu types), with `linear()`, `nearest()`, `set_filter`, `set_address_mode`, `set_anisotropic_filter`, and `as_wgpu() -> SamplerDescriptor<Option<&str>>` | matching fields and the first four helpers | ✅ | Mismatch `I5`, fixed alongside G8. The field set and `linear`/`nearest`/`set_filter`/`set_address_mode` already mirror Bevy; `as_wgpu()` and `set_anisotropic_filter()` are missing, so the render module cannot obtain a `wgpu::SamplerDescriptor` through Bevy's conversion surface. |
| G10 | **Design confirmation (not a mismatch):** `Image` uses `wgpu::TextureDescriptor`/`TextureViewDescriptor` directly for the *texture*, but keeps its *own* `ImageSampler`/`ImageSamplerDescriptor` and converts only in `as_wgpu()` | — | ✅ | Verified against `bevy_image/src/image.rs`: `texture_descriptor` and `texture_view_descriptor` are wgpu types (lines 588-614), while `ImageSampler`/`ImageSamplerDescriptor`/`ImageAddressMode`/`ImageFilterMode`/`ImageSamplerBorderColor` are Bevy types (lines 618-800) exposing `as_wgpu()` at line 856. The sampler types are Bevy-owned because they need defaults, serialization, and reflection that wgpu's types do not provide, so Epix must mirror them rather than alias wgpu's — the direct-wgpu allowance in the objective covers the texture descriptor but not the sampler. |


## Audit log

**Build-hygiene rule (confirmed twice in this effort).** Ninja only recompiles the
`.cpp` that owns a changed header when that header was located by position rather
than by name, so a header whose **type layout** changes can leave every consumer
object stale and produce a mixed-ABI binary. The editor tool has no attachment to
the object files, so this failure looks like a source bug. Detect it by comparing
the number of `Building CXX object` lines in the build log against the number of
translation units that include the changed header:

```powershell
$log = "build/msvc-reldeb-no-modules/<build>.log"
(Get-Content $log | Select-String 'Building CXX').Count
```
then list the consumers with the `grep` tool (`include <epix/image`) and compare.
If the compiles do not cover the consumers, the binary is unreliable and only a
`--target clean` + rebuild makes further debugging meaningful. Both image-batch
regressions below were this, not the source.

| Date | Evidence | Result |
| --- | --- | --- |
| 2026-09-11 | I1 field-level mapping pass, prompted by the module-priority review. Read Bevy `bevy_image/src/image.rs` `Image` and the Epix `Image` header and `render/src/image.cpp` naming the consumer. | Opened G1–G7. The `Format` enum is private to the image module (no consumer outside `epix_engine/image`), so replacing the intrinsic format representation has no external ripple; the only cross-module consumer is the render module's `format_cast`/`dimension_cast`. Recorded the two-step ordering: move width/height/format/dimension accessors onto the descriptor first, then delete the intrinsics, so the change stays reviewable. |
| 2026-09-11 | G1/G2/G7 implementation and sampler-surface verification against `bevy_image/src/image.rs`. | `Image` now owns `texture_descriptor` (`wgpu::TextureDescriptor`), `data_order`, and `texture_view_descriptor`; `width`/`height`/`depth_or_layers`/`depth`/`layers`/`type`/`format`/`format_info` all read from the descriptor, and the five intrinsics were deleted rather than duplicated. `ImageUsage` is now `assets::RenderAssetUsages`, matching every other render asset. The image module gained the `webgpu` dependency, owns the `Format`→`wgpu::TextureFormat` and `ImageType`→`wgpu::TextureDimension` mapping, and `RenderAsset<Image>::prepare_asset` consumes `asset.texture_descriptor` directly, deleting the render-side `format_cast`/`dimension_cast`. Verified: full no-modules build and all 73 CTest executables pass, including the screenshot readback round-trip and image parity tests. |
| 2026-09-11 | G8/G9 opened after review asked whether Bevy's sampler uses the wgpu descriptor as its texture descriptor does. | Confirmed from source that the answer differs by type: `texture_descriptor`/`texture_view_descriptor` **are** wgpu types, while `ImageSampler`/`ImageSamplerDescriptor`/`ImageAddressMode`/`ImageFilterMode`/`ImageSamplerBorderColor` are Bevy-owned types that convert via `ImageSamplerDescriptor::as_wgpu()`. Recorded as G8 (the `Descriptor` variant must carry its payload, so the separate `m_sampler_descriptor` member must go) and G9 (missing `as_wgpu()` and `set_anisotropic_filter()`). G10 records the design confirmation so the wgpu allowance is not misread as covering the sampler. |
| 2026-09-11 | **Second occurrence of the Ninja dispatch bug**, on the G8/G9 batch. `tests_header_image_image_plugin_tests` first looked like a hang, then reported an SEH `0xc0000005` inside `ImagePlugin::attach`; probes narrowed the failure to `Image::Image()`, a five-line constructor that cannot recurse. The build log contained **one** `Building CXX object` line (`image.cpp.obj`) while the `grep` tool found **51 translation units** including `epix/image.hpp` — and this batch changed `ImageSampler` from a bare enum into a payload-carrying `std::variant`, i.e. it changed `Image`'s layout. | Confirmed as the dispatch bug exactly as review warned: the binary was mixed-ABI. `--target clean` plus a rebuild resolved it, and `image_plugin_tests`, `image_parity_tests`, `screenshot_plugin_test`, and all 73 CTest executables passed afterwards. `Image`'s source was never at fault and the probes were removed before committing. The build-hygiene rule at the top of this log is the direct result: **count the log's `Building CXX` lines against the changed header's consumers before treating a runtime failure as a source bug.** |
| 2026-09-11 | G8/G9 implementation. `ImageSampler` is now a payload-carrying `std::variant` (`DefaultAlternative` / `DescriptorAlternative`); the separate `Image::m_sampler_descriptor` member is gone; `descriptor()`, `is_default()`, `linear()`, `nearest()`, and `get_or_init_descriptor()` follow Bevy's surface. `ImageSamplerDescriptor` gained `set_anisotropic_filter`, and `as_wgpu()` moved from the render module into the image module, so `RenderAsset<Image>` calls `sampler_descriptor()->as_wgpu()` and the render-side sampler casts were deleted. | Matches `bevy_image/src/image.rs:618-660` (`ImageSampler`) and `:803-870` (`ImageSamplerDescriptor`). New coverage in `image_parity_tests.cpp` asserts the carried payload, `linear()`/`nearest()`, `get_or_init_descriptor()` promotion and idempotence, `set_anisotropic_filter` forcing linear filters, and a field-by-field `as_wgpu()` mapping. One intentional translation divergence: wgpu-native's C API has no `AddressMode::ClampToBorder`, so `ImageAddressMode::ClampToBorder` clamps to edge; recorded as an FFI gap rather than a Bevy divergence. |
