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
| G7 | The render module consumes the image's declared texture descriptor rather than reconstructing it | `render/src/image.cpp` `format_cast(image::Format)` and `dimension_cast(image::ImageType)` rebuild the wgpu format and dimension at prepare time | ❌ | Mismatch `I1` + parent-module note. Bevy's render preparation reads `image.texture_descriptor` directly, so the mapping belongs to the image module. Fix: expose the mapping from the image module and have `RenderAsset<Image>` read the descriptor. This is the one place where the objective's "the render module should be up to date" note applies, and it is a *consequence* of the image-side fix rather than an independent render defect. |

## Audit log

| Date | Evidence | Result |
| --- | --- | --- |
| 2026-09-11 | I1 field-level mapping pass, prompted by the module-priority review. Read Bevy `bevy_image/src/image.rs` `Image` and the Epix `Image` header and `render/src/image.cpp` naming the consumer. | Opened G1–G7. The `Format` enum is private to the image module (no consumer outside `epix_engine/image`), so replacing the intrinsic format representation has no external ripple; the only cross-module consumer is the render module's `format_cast`/`dimension_cast`. Recorded the two-step ordering: move width/height/format/dimension accessors onto the descriptor first, then delete the intrinsics, so the change stays reviewable. |
