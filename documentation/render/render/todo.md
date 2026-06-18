# epix.render — TODO

No stub implementations (`assert(false)` or empty bodies that should be filled in)
were found in the exported API as of the current scan.  All exported methods have
real implementations in the corresponding `.cpp` source files.

Two code-level TODO comments were found in the implementation:

## Known Issues / Planned Work

### 1. DrawFunctions<P> read performance
**File:** `epix_engine/render/render/modules/render_phase.cppm`, line 289  
**Note:** `DrawFunctions<P>` uses `std::shared_mutex` for thread safety.  The
comment notes that switching to C++26 `<rcu>` would improve read-path
performance in the future.  No user-visible API change required.

### 2. MSVC AssetId partial specialisation workaround
**File:** `epix_engine/render/render/src/pipeline_server.cpp`, line 170  
**Note:** An internal cast from `AssetId<T>` to `UntypedAssetId` is required
as a workaround for MSVC partial specialisation limitations.  No user-visible
API change; affects only MSVC builds.

---

## Potential Ergonomic Gaps (not stubs)

1. **Visibility culling** — `VisibleEntities` is populated but there is no
   built-in frustum-culling system.  Users must populate it manually or provide
   their own culling plugin.

2. **Depth format customisation** — `ViewDepth` always uses the device default
   depth format.  There is no API to select a custom format (e.g. `Depth24Plus`
   vs `Depth32Float`) per camera.

3. **Pipeline specialisation constants** — `RenderPipelineDescriptor` and
   `ComputePipelineDescriptor` have no field for `wgpu::ConstantEntry`
   (WebGPU pipeline overridable constants).

4. **Async pipeline status callbacks** — there is no event or callback fired
   when a queued pipeline finishes compilation.  Users must poll
   `get_pipeline_state()` / `get_render_pipeline()` each frame.
