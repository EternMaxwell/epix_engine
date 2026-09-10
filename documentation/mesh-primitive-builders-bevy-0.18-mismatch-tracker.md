# Bevy 0.18 Mesh Primitive Builders — Numbered Mismatch Tracker

Audit scope: `epix_engine/mesh` primitive builders (`Meshable` / `MeshBuilder`
surface) against Bevy `v0.18.0` (commit
`5f8270f2e049f90139a503d1e930070d926f9427`) sources in `tmp/bevy-0.18.0`:

| Bevy source | Epix counterpart |
| --- | --- |
| `crates/bevy_math/src/primitives/mod.rs` | `epix_engine/mesh/include/epix/mesh/primitives.hpp` |
| `crates/bevy_math/src/primitives/dim2.rs` | `epix_engine/mesh/include/epix/mesh/primitives.hpp`, `src/primitives.cpp` |
| `crates/bevy_mesh/src/primitives/mod.rs` | `epix_engine/mesh/include/epix/mesh/mesh.hpp` (`MeshBuilder`, `Meshable`) |
| `crates/bevy_mesh/src/primitives/dim2.rs` | `epix_engine/mesh/src/primitives.cpp` |

This tracker is the working batch list for the primitive-builder parity pass.
It refines the batch rows `M19b2` / `M19b3` of
`documentation/mesh-core-graph-bevy-0.18-match-status.md`, which remains the
module-wide source of truth. Every row is retained until it is fixed or the
user explicitly accepts it as intentional, deferred, or out of scope.

Status legend: ✅ aligned · ❌ mismatch to fix · ⚠️ intentional divergence ·
🚫 missing (deferred to a later batch) · REVIEW · N/A.

Reproduction artifacts for this batch (capture helpers, frame PNGs, example
logs) live under `tmp/m19b3-visual/`, not under `documentation/`. Repository
documentation holds only the tracker itself.

## Batch: M19b2 / M19b3 linear and polygon builders

| ID | Bevy entity / behavior | Epix counterpart | Status | Notes |
| --- | --- | --- | --- | --- |
| B1 | `ConvexPolygonMeshBuilder::build` inserts only `Mesh::ATTRIBUTE_POSITION` and `Indices::U32` on a `TriangleList`; positions are `[vertex.x, vertex.y, 0.0]`; indices are `for i in 2..len { [0, i-1, i] }` | `ConvexPolygonMeshBuilder::build` | ❌ | Epix additionally inserts `ATTRIBUTE_NORMAL` (all `(0,0,1)`) and an `ATTRIBUTE_UV_0` computed from the vertex AABB. Bevy's builder emits neither. Fix: drop both attributes. `bevy_mesh/src/primitives/dim2.rs:429-448`. |
| B2 | `Segment2dMeshBuilder::build` inserts only `ATTRIBUTE_POSITION` + `Indices::U32([0, 1])` on `LineList` | `Segment2dMeshBuilder::build` | ✅ | Field name (`segment`), public-ness, `Meshable for Segment2d`, `Segment2d::mesh`, and the default asset usage all match. |
| B3 | `Polyline2dMeshBuilder::build` inserts `Indices::U32` first (`flat_map(|i| [i, i+1])` over `0..len-1`) then `ATTRIBUTE_POSITION`, on `LineList` | `Polyline2dMeshBuilder::build` | ✅ | Index-then-attribute insertion order, `LineList`, and `flat_map` pair order match. `Polyline2dMeshBuilder` keeps its `polyline` member private as in Bevy. |
| B4 | `Polyline2d::with_subdivisions` pushes `subdivisions + 2` vertices from `start + step * i`, `step = (end - start) / (subdivisions + 1)` | `Polyline2d::with_subdivisions` | ✅ | Same count, same step, same float ordering. |
| B5 | `Polyline2d::default` = `[-0.5, 0.0] .. [0.5, 0.0]`; `Polyline2d::new`/`FromIterator`; `Segment2d::default` = same endpoints | `Polyline2d` / `Segment2d` defaults | ✅ | Defaults match. Bevy's `Polyline2d::new(impl IntoIterator<Item = Vec2>)` maps to the Epix `explicit Polyline2d(std::vector<glm::vec2>)` constructor, which is the C++ constructor adaptation already accepted for this module. |
| B6 | `ConvexPolygon::new` classifies every consecutive vertex triple with `Triangle2d::winding_order()` (`perp_dot` cross product, `±f32::EPSILON` dead band → `Invalid`) and compares against the `(len-1, 0, 1)` reference triple; `ConvexPolygonError::Concave` display text `"The created polygon is not convex"` | `ConvexPolygon::create` | ✅ | Numerically identical cross product, identical epsilon dead band, identical reference triple, identical loop range `1..len`, identical error text. |
| B7 | `ConvexPolygon::new_unchecked`, `ConvexPolygon::vertices() -> &[Vec2]`, `ConvexPolygon` keeps `vertices` private | `ConvexPolygon::new_unchecked` / `vertices()` | ✅ | `std::span<const glm::vec2>` is the C++ slice translation; storage stays private. |
| B8 | `Meshable for ConvexPolygon` copies the vertex list (`self.vertices().to_vec()`) | `ConvexPolygon::mesh` | ✅ | Copies into the builder; the shape keeps ownership. |
| B9 | `impl From<ConvexPolygon> for Mesh`, `impl From<Segment2d> for Mesh`, `impl From<Polyline2d> for Mesh` (`shape.mesh().build()`) | `Mesh(const Meshable Shape&)` converting constructor | ✅ | `mesh.hpp:552` provides the generic `Meshable → Mesh` conversion for all three shapes. Toolbox `Segment2dMeshBuilder::new(Segment2d)` (`mesh()` uses it) maps to the explicit `Segment2dMeshBuilder(Segment2d)` constructor. |
| B10 | `WindingOrder` lives in `bevy_math::primitives` and is returned by `Triangle2d::winding_order()`; `ConvexPolygon::triangle_winding_order` delegates to `Triangle2d::new(a, b, c).winding_order()` | `WindingOrder` in `epix::mesh`; local lambda in `ConvexPolygon::create` | 🚫 | `Triangle2d` itself is a separate missing shape (B12). The local computation is numerically identical (B6), so this stays deferred until `Triangle2d` is added rather than moving `WindingOrder` to a nonexistent math module. |
| B11 | `Segment2d::from_direction_and_length(direction: Dir2, length)`; `Dir2` is a guaranteed-unit vector and `Dir2::new` returns `Err(InvalidDirectionError)` instead of panicking | `Segment2d::from_direction_and_length(glm::vec2, float)` normalizes internally and throws `std::invalid_argument` on a zero vector | ⚠️ | Intentional: Epix has no standalone math-primitives module (accepted in `M19a`), so there is no `Dir2` type to accept. The Epix overload accepts an arbitrary direction and normalizes it, and reports the degenerate case as a typed exception rather than a panic. The unit-direction result is identical: `endpoints = ∓0.5 * length * normalize(direction)`. |
| B12 | Extended 2D shapes still missing from `bevy_math::primitives`: `Polygon`, `Triangle2d`, `Rhombus`, `Annulus`, `Ring`, `Capsule2d`, `Plane2d`, and the `Primitive2d` marker | none | 🚫 | Deferred: tracked as the remainder of `M19b2`/`M19b3` in the module-wide tracker. Only the shapes needed by this batch's builders were added so far. |
| B13 | Degenerate inputs: `Polyline2dMeshBuilder::build` with `vertices.len() < 2` panics (subtraction overflow on `len - 1`); `ConvexPolygonMeshBuilder::build` with `len < 2` panics on `Vec::with_capacity((len - 2) * 3)`; `ConvexPolygon::new` with `len < 3` panics on `triangle_winding_order(len - 1, 0, 1)` | Epix returns an empty/partial mesh and `ConvexPolygon::create` returns `Concave` | ⚠️ | Intentional: Epix defines out-of-contract input as empty output instead of a Rust arithmetic panic. All well-defined inputs are byte-identical, so no fixed data can change. Recorded here because the observable contract differs for malformed input. |
| B14 | `Extrudable for ConvexPolygonMeshBuilder` (`PerimeterSegment::Flat` over `0..len` plus a closing `0`) | none | 🚫 | Deferred: `Extrudable` / `Extrusion` is `M19f` in the module-wide tracker. |
| B15 | `ConvexPolygonMeshBuilder` / `Polyline2dMeshBuilder` `Reflect` + `Clone` + `Debug` derives | plain C++ aggregates with implicit copy | N/A | Epix has no reflection registration for these value types; accepted throughout the mesh module (`M19a`). |

## Audit log

| Date | Evidence | Result |
| --- | --- | --- |
| 2026-09-11 | M19b2/M19b3 batch: read Bevy `v0.18.0` `bevy_mesh/src/primitives/dim2.rs` (`ConvexPolygonMeshBuilder`, `Segment2dMeshBuilder`, `Polyline2dMeshBuilder`), `bevy_math/src/primitives/dim2.rs` (`Segment2d`, `Polyline2d`, `ConvexPolygon`, `Triangle2d::winding_order`) and `bevy_math/src/primitives/mod.rs` (`WindingOrder`) line by line against the in-flight Epix `primitives.hpp`/`primitives.cpp`. | Opened B1–B15. B1 is a real mismatch (Epix's convex-polygon mesh carried `ATTRIBUTE_NORMAL` and `ATTRIBUTE_UV_0`, Bevy's does not) and is fixed in this batch; B2–B9 are aligned; B10, B12, B14 remain deferred to `Triangle2d`/`M19b3`/`M19f`; B11, B13 are recorded intentional divergences; B15 is not applicable. |
| 2026-09-11 | B1 fix verification. `Reconfigure`/incremental build of `msvc-reldeb-no-modules` (MSVC Insider 14.52, `RelWithDebInfo`, `EPIX_CXX_MODULE=OFF`, `EPIX_IMPORT_STD=OFF`) for the mesh library, `tests_header_mesh_module_test`, and `examples_header_mesh_linear_polygon_primitives`; then the complete tree (241 actions) and all 73 CTest executables serially. `clang-format --style=file` reports zero replacements on all six touched sources. | The focused mesh executable passes 29 tests, including the rewritten `ConvexPolygonMatchesBevyValidationAndFanData` (asserts `ATTRIBUTE_NORMAL`, `ATTRIBUTE_UV_0`, and `ATTRIBUTE_COLOR` are all absent), the new `ConvexPolygonRejectsDegenerateVertexCountsWithoutPanicking`, and the new `LineListPrimitivesEmitOnlyBevyPositionAttribute`. All 73 CTest executables pass. `Segment2d`, `Polyline2d`, and `ConvexPolygon` have no consumers outside the mesh module, so no other module needed a change. |
| 2026-09-11 | B1 visual verification with `PrintWindow(PW_CLIENTONLY)` on the process-owned `GLFW30` client windows, three frames each, driver overlay excluded by cropping below `y=150` (the overlay is live and not part of the scene). | `linear_polygon_primitives` (1000x550) logs `Bevy-aligned attributes: segment=[Vertex_Position (2 vertices)] polygon=[Vertex_Position (5 vertices)] polyline=[Vertex_Position (5 vertices)]`, rendering the orange diagonal segment, the mint convex pentagon, and the blue polyline — the pentagon still renders solid despite the removed UV/normal attributes because `Mesh2dPipeline` requests only the attributes the layout has. `mesh_rendering` (1280x720, center `(198,197,163)`), `mesh_batching` (1280x720, center `(228,228,179)`), and `mesh_alpha_mask` (960x540, center `(253,160,80)`) are unchanged and match their earlier baselines. Every scene-only crop was byte-identical across all three frames (`318B88D4…`, `D929F503…`, `9643EAF9…`, `CB58CDB7…`), every stderr log was empty, and no log contained `[error]`, `[critical]`, validation, panic, or exception output. |
| 2026-09-11 | Capture-harness correction. The first capture pass set `SPDLOG_LEVEL=trace`, which produced ~9 MB of per-frame trace output and throttled the examples to ~10 FPS. Re-captured with logging off, recording the driver overlay frame-rate crop as evidence alongside each frame. | Same binaries, same preset: `linear_polygon_primitives` 1408 FPS (1.4 ms with the overlay visible), `mesh_alpha_mask` 1508 FPS (0.6 ms), `mesh_batching` 831 FPS (1.2 ms), `mesh_rendering` 795 FPS (1.1 ms). Trace logging is therefore reserved for diagnosing unidentified problems, never for routine visual verification. |
