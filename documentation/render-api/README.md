# Render API Candidate Study

This directory compares eight **complete, alternative render-module architectures**.
They are not variations of the existing render graph/phase API, and they do not
share a prescribed sequence such as extraction, queueing, preparation, or graph
execution. A candidate may use equivalents where its own model needs them, but it
must define them itself.

The only common constraints are imposed by the repository rather than by a proposed
renderer:

- The design lives in `epix.render`; it cannot add to `App`, ECS, runners, or
  window backends.
- Main and render worlds remain separate. The current runner owns their handoff and
  possible overlap; a render-only design may not promise a new queueing policy.
- A main-world reference obtained through `Extract<T>` is valid only while the
  synchronous extraction system runs. No candidate may retain it in render state.
- Public APIs must be expressible as plugins, free functions, components, resources,
  events, ordinary system/query types, and—where worthwhile—custom
  `QueryData`/`QueryFilter`/`SystemParam` specializations.
- The device, surfaces, assets, and shaders remain WebGPU-backed. A candidate may
  improve their ownership model but does not introduce a generic graphics backend.

## What makes a candidate complete

Each candidate document independently answers all of these questions:

1. What application code creates renderable content, and how can it opt out?
2. What exactly crosses from main to render world: entity projection, immutable
   records, commands, handles, or another value model?
3. Which render-world structures hold scene state, views, materials, instance data,
   GPU objects, resource readiness, and temporary frame state?
4. How does the candidate derive work, establish ordering/dependencies, and exploit
   parallelism? Its answer may be a compiled plan, reactive updates, transactions,
   or anything else; it is not required to resemble the current stages.
5. How are target ownership, multi-view composition, visibility, transparent/opaque
   rules, resize, async assets/pipelines, device loss, and overlays defined?
6. How does a third-party feature add a renderer and how does an expert use raw
   WebGPU without violating the model?

A custom ECS extension is never included merely to sound ergonomic. The design table
in each file compares it to ordinary queries/resources/commands, describes scheduler
access and scoped lifetime, and rejects it if conventional ECS remains clearer.

## Shared evaluation scene

Candidates are compared by observable behavior, not required internals. Their
illustrative examples cover a two-camera world with opaque meshes, transparent
sprites, an off-screen effect, a late overlay, resize, pending/failed assets and
pipelines, and one advanced direct-WebGPU integration. Each candidate adds its own
focused demonstration.

## Current-renderer evidence

The current sprite, mesh, and text integrations separately implement extracted
entities, pipeline specialization, view filtering, instance staging, bind groups,
phase-item queues, and graph attachment. The current system also needs deliberate
care around view-local prepared offsets, deterministic camera ordering, and late
ImGui-style surface overlays. These are problems the candidates must solve, not
interfaces they must preserve.

Research contrast points are [Unity Entities Graphics](https://docs.unity.cn/Packages/com.unity.entities.graphics%401.4/manual/overview.html),
[Filament's Engine/RenderableManager](https://github.com/google/filament/blob/main/filament/include/filament/Engine.h),
and [Godot's RenderingServer](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html).
They inform tradeoff analysis only; no candidate imports their architecture.

## Review-driven implementation constraints

The first source review added requirements that every candidate must make concrete:

- **Frame epochs, not borrowed world state.** Under the GLFW/SFML runner, render
  frame N can run while main frame N+1 updates, but the runner joins render N before
  it runs extraction for N+1. A candidate must label every extracted/render frame
  with a RenderEpoch and mutate render-owned persistent state only during this
  joined handoff. Nothing from Extract<T> survives the extraction system.
- **Replace global entity clearing.** The current RenderPlugin clears every render
  entity after cleanup. A candidate with persistent proxies/archetypes/intent
  catalogues must replace that implementation with a FrameEntityRegistry that
  destroys only entities tagged FrameLocal{epoch}; persistent data belongs in
  resources or persistent render entities explicitly owned by the candidate.
- **Make GPU completion real, subject to a wrapper prototype.** The pinned
  `libs/webgpu-wrapper/webgpu/webgpu.h` exposes
  `wgpuQueueOnSubmittedWorkDone(WGPUQueue, WGPUQueueWorkDoneCallbackInfo)`, returning
  `WGPUFuture`, with success/error/instance-dropped statuses. A render-only
  SubmissionTracker must wrap it, enqueue only `{submission_id, status}` from the
  callback, and drain that inbox during the next render update. Callback mode/thread
  delivery, cancellation, and device-loss behavior are a prototype gate before any
  candidate can rely on completion-gated reuse; device loss abandons old epochs.
- **Define device recovery from actual render-world window data.** `AnonymousSurface`
  is consumed during initial adapter selection and cannot be retained. Recovery must
  instead use the copied `window::SurfaceCreation` already carried by each
  render-world `window::ExtractedWindow`, which normal extraction updates/removes.
  Device callbacks capture a weak shared endpoint that only enqueues lifecycle
  events; detach closes it before native teardown. A DeviceEpoch state machine
  (Operational, Lost, Recreating, Failed) consumes device callbacks, prevents target
  acquisition/submission while lost, recreates adapter/device/resources internally,
  and invalidates all GPU caches by epoch. This stays inside render; it does not
  modify runners or window backends.
- **One target lease owner.** A TargetLease owns acquire, first clear/load choice,
  final store/present, and error release for one surface/image in one RenderEpoch.
  Different command encoders may record independent work, but a render pass for the
  same attachment has one declared owner and sequential pass boundaries.
- **Total view ordering.** Every target sorts views by
  (target identity, target generation, camera order, stable camera source ID).
  Ties are impossible after the stable ID. The target program owns attachment
  load/store and viewport/scissor policy for this sequence.

These are implementation requirements, not a common candidate API. Each design
chooses where its state lives and how it presents these mechanisms.

## Required prototype scorecard

Every candidate prototype must report the following against the same scene and the
current renderer where a comparable measurement exists.

| Concern | Required evidence |
| --- | --- |
| Cross-world ownership | RenderEpoch trace proving no borrowed main-world value survives extraction |
| Removal/retirement | despawn + entity-reuse scenario; released GPU slot only after its submission completes |
| Multi-view data | two cameras/targets, separate ranges, deterministic output ordering/hash |
| Surface lifetime | TargetLease trace for normal, zero-size, timeout/outdated, and abandoned frames |
| Composition | first-clear, later-load, depth/viewport/scissor, overlay, and final-present trace |
| Async resources | Pending then Ready then Failed asset/pipeline behavior without stale work |
| Device loss | Operational -> Lost -> Recreating/Failed transition, cache invalidation, and skipped frame behavior |
| Native control | custom WebGPU recording through frame-owned encoder; no direct Queue::submit |
| Parallel work | task partitions, CPU time, allocations/entity churn, command buffers, submissions |

All illustrative C++ must use shapes compatible with the existing ECS vocabulary:
systems receive Query<Item<...>, Filter<...>>, Res/ResMut, Commands, and Extract
parameters. A candidate may later introduce a named alias or custom query/system
parameter, but it must define that specialization's state, access, validation, and
non-escaping Item lifetime. It must not rely on a fictional nested Item type.

## Detailed-use-case portfolio

The candidate documents are not evaluated from a single sprite draw. Each now carries
at least three concrete, intentionally contrasting workflows. Together they force
the API to expose normal content, composition, failure/lifetime behavior, and a
native extension boundary instead of hand-waving those concerns as implementation
details.

| Candidate | Ordinary scene pressure | Composition/lifecycle pressure | Distinctive custom or advanced pressure |
| --- | --- | --- | --- |
| 01 Projection | one source with sprite + mesh aspects | HDR bloom survives proxy state across resize | leased gizmo/native product |
| 02 Query | independent sprite, mesh, and shadow queries | split views plus picking/outline program | compute+raster particles in a program slot |
| 03 GPU database | dense opaque foliage/mesh tables | exact alpha for split views | water table class with compute dependency |
| 04 Rules | structural rule and rule-local disable | target-only bloom rule | third-party final metrics overlay |
| 05 Reactive | sparse transform changes | view resize and asset failure/retry | declared heatmap native write |
| 06 Snapshots | filtered ECS publication | capture/replay, remote input, target remap | particle compute-to-indirect reducer |
| 07 Intents | per-view draw intents plus rewrite | HDR/bloom/composite/overlay expansion | frozen particle-domain intent |
| 08 Transactions | one-to-many scene objects for two windows | HDR/post chain plus parallel thumbnail | progressive path-trace transaction |

For every workload, a future prototype must preserve the scorecard above. A concise
example is not a proof that the candidate supports a case: prototype evidence must
show concrete target instances, copied cross-world inputs, declared resource uses,
and completion-gated native-resource retirement.
