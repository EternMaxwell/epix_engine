# Fixed-Point 16.16 Migration Tracker (liquid_eulerian)

Target file: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp)

Status legend:
- [ ] not started
- [~] in progress
- [x] done

## 1) Core numeric model (C++)
- [~] Define 16.16 fixed type and helpers (convert, mul/div, clamp, lerp)
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L19)
- [~] Replace scalar constants with fixed equivalents where used in step logic
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L19)

## 2) Fluid storage + CPU stepping
- [x] Change Fluid grids from float storage to int32 fixed storage (`D/newD/P/U/newU/V/newV/fluxU/fluxV`)
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L74)
- [x] Migrate accessors/setters (`getD/getP/getU/getV/setD/setP/setU/setV`) to fixed-domain API
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L106)
- [x] Migrate interpolation helpers (`sampleU/sampleV`) to fixed arithmetic (or fixed-compatible path)
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L135)
- [ ] Migrate CPU-only simulation helpers:
  - [x] `extrapolateVelocity`
  - [x] `applyViscosity`
  - [x] `applySurfaceTension`
  - [x] `advectVelocityRK2`
  - Location start: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L225)
- [~] Migrate `physicsStep` and `solve` scalar math to fixed-domain variables
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L386)

Progress note:
- [~] `physicsStep -> gpu_run_full_step_chain -> dispatch_full_step_chain` now passes `dt/sticky` as 16.16 fixed, with temporary float conversion only at current uniform packing boundary.

## 3) GPU host-side buffers + upload/readback
- [x] Change GPU host mirrors to int32 fixed (`host_d/host_u/host_v/host_p`)
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L562)
- [x] Change byte sizing from `sizeof(float)` to fixed storage size
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L570)
- [~] Update `upload_from_sim` conversion (float<->fixed boundary if needed)
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1829)
- [~] Update readback methods to decode fixed data correctly:
  - [x] `readback_to_sim`
  - [x] `readback_density_to_sim`
  - [x] `readback_all_to_sim`
  - Location start: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L2098)

## 4) GPU parameter packing
- [~] Replace float uniform param pack (`AdvectParam` / `SimParam`) with fixed/int layout
  - Location references:
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1882)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1908)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1927)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1968)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1996)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L2279)

## 5) WGSL shader migrations (all to fixed-point)
- [~] Pressure even shader `kShaderEven`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L579)
- [~] Pressure odd shader `kShaderOdd`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L638)
- [~] Advection U shader `kShaderAdvectU`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L697)
- [~] Advection V shader `kShaderAdvectV`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L770)
- [~] Density shader `kShaderDensity`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L843)
- [~] Gravity shader `kShaderGravity`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L918)
- [~] Post U shader `kShaderPostU`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L943)
- [~] Post V shader `kShaderPostV`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L973)
- [~] Surface U shader `kShaderSurfaceU`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1003)
- [~] Surface V shader `kShaderSurfaceV`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1036)
- [~] Viscosity U even shader `kShaderViscUEven`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1069)
- [~] Viscosity U odd shader `kShaderViscUOdd`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1104)
- [~] Viscosity V even shader `kShaderViscVEven`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1139)
- [~] Viscosity V odd shader `kShaderViscVOdd`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1174)
- [~] Wall-friction U shader `kShaderViscWallU`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1209)
- [~] Wall-friction V shader `kShaderViscWallV`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1251)
- [~] Extrapolate even shader `kShaderExtrapCellEven`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1293)
- [~] Extrapolate odd shader `kShaderExtrapCellOdd`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1360)
- [~] Clamp U shader `kShaderClampU`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1427)
- [~] Clamp V shader `kShaderClampV`
  - Location: [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1444)

## 6) Dispatch + API surface updates
- [~] Update dispatch signatures and helper APIs from float params to fixed/int params where applicable
  - Locations:
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L1882)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L2279)
  - [epix_engine/experimental/tests/visual/liquid_eulerian.cpp](../epix_engine/experimental/tests/visual/liquid_eulerian.cpp#L2446)

## 7) Validation
- [x] Build passes
- [x] Runtime smoke test passes
- [ ] Visual parity check completed (no severe regression)
- [ ] Tracker self-audit completed (all [ ] reviewed before declaring done)

## Working Notes
- 2026-03-18: Tracker created. Refactor not complete yet.
- 2026-03-18: Migrated Fluid grid storage to `int32` fixed-point backing and switched getters/setters to conversion boundaries.
- 2026-03-18: Migrated full-step GPU API parameters (`dt`, `sticky`) to 16.16 in C++ call chain.
- 2026-03-18: Migrated GPU host mirrors (`host_d/u/v/p`) and byte-size constants to fixed-point domain, with temporary float staging bridge for current float storage WGSL.
- 2026-03-18: Migrated `AdvectParam` / `SimParam` uniform packing to `i32` in WGSL and C++ queue writes; shader-side uses explicit fixed->float decode helpers.
- 2026-03-18: Migrated `sampleU/sampleV` interpolation weights/blends to fixed-point arithmetic over raw fixed grid values.
- 2026-03-18: Fixed CPU fallback `advectVelocityRK2` output writes to use explicit fixed conversion (no float->int truncation).
- 2026-03-18: Migrated `extrapolateVelocity` neighbor accumulation/averaging to fixed-point core math.
- 2026-03-18: Migrated `applyViscosity` and `applySurfaceTension` update math to fixed-point core paths with direct fixed grid writes.
- 2026-03-18: `physicsStep` active/core classification and mass accumulation switched to fixed-domain reads; `solve` max-velocity scan switched to fixed-domain reads.
- 2026-03-18: Pressure shaders now use fixed-point `P` storage (`array<i32>`) with float->fixed accumulation conversion; host `p_buf` upload switched to fixed mirror data.
- 2026-03-18: Pressure even/odd shaders switched pressure-correction core arithmetic (`div/targetDiv/weight/omega`) to fixed-point operations, with velocity writes converted back to float for shared buffers.
- 2026-03-18: Fixed runtime `wgpu` panic in pressure shaders by removing unsupported WGSL `i64` usage from fixed multiply helpers.
- 2026-03-18: Density shader core transport flux/update arithmetic switched to fixed-point operations.
- 2026-03-18: Gravity and surface shaders switched core update formulas to fixed arithmetic (float storage compatibility retained for shared buffers); runtime validation passed without `wgpu` panic.
- 2026-03-18: Advection U/V shaders switched RK2 midpoint/backtrace coordinate integration to fixed-point arithmetic (float sampling/storage compatibility retained); build and runtime smoke test both pass.
- 2026-03-18: Post U/V shaders switched velocity clamp/solid-zero logic to fixed arithmetic (float storage compatibility retained); build and runtime smoke test pass.
- 2026-03-18: Viscosity U/V even+odd shaders switched diffusion update core math to fixed arithmetic (float storage compatibility retained); build and runtime smoke test pass.
- 2026-03-18: Wall-friction U/V shaders switched sticky damping core from float `pow` to fixed multiplicative damping; build and runtime smoke test pass.
- 2026-03-18: Extrapolate even/odd and clamp U/V shaders switched core averaging/clamping arithmetic to fixed-point operations (float storage compatibility retained); build and runtime smoke test pass.
- 2026-03-18: Dispatch/API path migrated further to fixed parameters (`dt/sticky` as `fx32`) and viscosity derived params (`iterations/nu/wallFriction`) now computed in fixed domain; build and runtime smoke test pass.
- 2026-03-18: Migrated GPU density storage path to fixed (`D/outD` as `array<i32>` across active shaders), switched `d_buf` upload/readback to direct fixed data, and updated pressure/density/gravity/surface/viscosity/wall/extrap D reads accordingly; build and runtime smoke test pass.
- 2026-03-18: Cleaned remaining D-path threshold checks in viscosity/extrapolation shaders to pure fixed comparisons (removed extra float decode in those checks); build and runtime smoke test pass.
- 2026-03-18: Introduced shared fixed scalar constants (`kFx005/kFx01/kFx02/kFx08/kFx13/...`) and replaced repeated runtime literal conversions in CPU/dspatch paths; build and runtime smoke test pass.
- 2026-03-18: CPU `advectVelocityRK2` midpoint/backtrace integration migrated to fixed-core arithmetic (sampling kept at explicit float boundary only); build and runtime smoke test pass.
- 2026-03-18: `physicsStep/solve` advanced further to fixed-domain timestep/mass correction control (`fx_div`, fixed dt stepping, fixed diff clamp) and removed int64->fx32 truncation in total-mass accumulation path; build and runtime smoke test pass.
- 2026-03-18: Pressure even/odd shaders now update velocity corrections using fixed-domain `u/v` core math (single float conversion only at storage write boundary); build and runtime smoke test pass.
- 2026-03-18: Fluid mass bookkeeping moved further to fixed (`target_total_mass/current_total_mass` fields, brush mass delta updates, and `physicsStep` mass targets now fixed-domain); build and runtime smoke test pass.
- 2026-03-18: Resolved runtime WebGPU validation crash after large U/V `array<i32>` batch migration by fixing WGSL type mismatches in pressure/advection/extrapolation shaders (removed double `f32_to_fx` on i32 buffers and float writes into i32 storage); build and runtime smoke test pass.
- 2026-03-18: Advect U/V shaders migrated further to fixed-core sampling path (`sample_u_fx/sample_v_fx` bilinear in 16.16) and RK2 backtrace now stays in i32 fixed from velocity fetch through output write; build and runtime smoke test pass.
- 2026-03-18: Density transport shader flux path now uses fixed velocity reads directly (`u_at_fx/v_at_fx`) and removed per-face float velocity conversions before flux multiply; soft density clamp remains explicit float boundary; build and runtime smoke test pass.
- 2026-03-18: Pressure even/odd shaders removed float constant conversion path (`f32_to_fx(0.1)`/`f32_to_fx(OMEGA)`) by switching to fixed constants (`TARGET_DIV_GAIN_FX`/`OMEGA_FX`), and removed now-unused float helper functions in these two shaders; build and runtime smoke test pass.
- 2026-03-18: Advect U/V shaders cleaned up residual unused float conversion helpers after fixed-sampling migration (`f32_to_fx/fx_to_f32` removed from these two shader blocks); build and runtime smoke test pass.
- 2026-03-18: Density shader soft-limit path migrated from float `exp` to fixed-point piecewise easing (`soft_bound_density_fx`), with fixed max/start/zero thresholds and direct fixed output write; build and runtime smoke test pass.
- 2026-03-18: Density soft-limit bugfix: replaced coarse fixed easing with LUT-based fixed approximation of `1-exp(-t)` over `t in [0,8]` plus linear interpolation, preserving fixed pipeline while matching prior behavior much closer; build and runtime smoke test pass.
- 2026-03-18: CPU `Fluid::soft_bound_density` and `setD` now use fixed LUT soft-bound path (`soft_bound_density_fx`) aligned with WGSL, and removed stale CPU float density-bound constants; build and runtime smoke test pass.
- 2026-03-18: Surface U/V shaders migrated further to fixed-core contribution logic (fixed thresholds + fixed density gradients, removed float contrib path), and removed unused float conversion helpers from gravity/post/clamp shaders; build and runtime smoke test pass.
