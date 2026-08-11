# Architecture Overview

This document is a high-level map of Epix Engine. It explains how the major
systems relate to each other and points to the module references for API details.

Epix is built on two foundation modules. `epix.ecs` owns data and execution;
`epix.app` owns application lifecycle and composes ECS schedules, plugins,
sub-apps, states, and runners. Feature modules depend on one or both layers.

## ECS and Application Layers

The main types are divided as follows:

- `epix.ecs`: `World`, entities, components, entity-backed resources, bundles,
  queries, events, systems, schedules, change detection, hierarchy, required
  components, and removed-component tracking.
- `epix.app`: `App`, built-in schedule order, plugins, runners, states,
  sub-apps, and main-to-sub-app extraction.

The standard frame flow is expressed as schedules. Built-in schedules include
startup, update, fixed update, and exit phases. Systems declare their inputs in
their parameter list, for example `Res<T>`, `ResMut<T>`, `Commands`, `Query<...>`,
and `EventReader<T>`.

Read more in [ecs/quick.md](ecs/quick.md), [ecs/world.md](ecs/world.md),
[ecs/schedule.md](ecs/schedule.md), and [app/quick.md](app/quick.md).

## ECS Data Access

Systems do not manually lock component storage. Instead, system parameters expose
the data they read and write. The scheduler uses that access metadata to order
systems and run compatible work concurrently.

Common access patterns:

- `Res<T>` and `ResMut<T>` for resources.
- `Query<Item<...>, Filter<...>>` for component iteration.
- `Commands` for deferred entity/resource mutations.
- `EventReader<T>` and `EventWriter<T>` for double-buffered events.
- `RemovedComponents<T>` for per-system removal/despawn observation.
- `Local<T>` for per-system local state.

Queries support built-ins such as entity IDs, optional components, changed refs,
and filters such as `With<T>`, `Without<T>`, and `Or<...>`.

Resources use the same component storage as entities. Each resource type has a
canonical entity marked with `IsResource`, so resource and ordinary component
access to the same type conflict correctly in the scheduler.

See [ecs/system-params.md](ecs/system-params.md), [ecs/query.md](ecs/query.md),
[ecs/query-built-ins.md](ecs/query-built-ins.md), [ecs/events.md](ecs/events.md),
and [ecs/removed-components.md](ecs/removed-components.md).

## Schedule Execution

Epix has two levels of concurrency:

1. **Within a world**: schedule executors run systems in parallel when their data
	access does not conflict. Executors derive dependency edges from system
	metadata and apply deferred commands at safe points.
2. **Between worlds**: the main world and render world can run concurrently at
	the runner level. They do not share ECS storage directly.

The main app schedule order includes startup, update, fixed update, state
transition, and exit schedules. Time-sensitive systems can use `epix.time` to run
inside fixed-timestep schedules such as `FixedUpdate`.

See [app/built-in-schedules.md](app/built-in-schedules.md) and
[time/quick.md](time/quick.md).

## Plugin Lifecycle

Plugins add resources, systems, events, schedules, sub-apps, and other plugins.
The typical lifecycle is:

- `attach(App&)`: register most resources and systems when the plugin is added.
- `ready(App&)`: complete setup after all plugins have had a chance to attach.
- `detach(App&)`: perform shutdown-sensitive cleanup after the app exits.

Feature modules expose plugins so applications can opt into only the engine
subsystems they use.

See [app/app.md](app/app.md) and [app/quick.md](app/quick.md).

## Render Sub-App and Extraction

Rendering lives in a dedicated render sub-app. The main world owns live gameplay
state. The render world owns GPU-facing state. During the extract phase, the
render app temporarily receives a reference to the main world through
`Extract<T>` parameters, then writes render-owned data. The two worlds do not
share permanent ECS storage, although mutable non-deferred extraction parameters
are supported for deliberate move/take operations.

The documents under `render/render` describe the renderer implemented today.
The separate `render-api` tree is a future design plan and is not the current
public API.

The render flow is roughly:

1. Main-world systems update gameplay state.
2. Extraction transfers or copies selected resources, components, and assets
	 into render-owned state.
3. Render-world systems prepare GPU resources, queue phase items, sort phases,
	 and execute the render graph.
4. The render graph submits WebGPU command buffers.

Key render concepts:

- `RenderPlugin`: creates WebGPU device state and the render sub-app.
- `ExtractSchedule`: copies data from the main world to the render world.
- `RenderGraph`: a directed acyclic graph of rendering nodes.
- `CameraBundle`: camera, projection, transform, render target, and visibility.
- `PipelineServer`: async render/compute pipeline cache.
- `RenderAsset<T>` and `RenderAssets<T>`: bridge loaded assets to GPU resources.
- `RenderPhase<P>`: sorted draw lists for ECS-driven rendering.

See [render/render/quick.md](render/render/quick.md),
[render/render/render-plugin.md](render/render/render-plugin.md),
[render/render/render-graph.md](render/render/render-graph.md),
[render/render/camera-view.md](render/render/camera-view.md), and
[render/render/pipeline.md](render/render/pipeline.md).

## Assets and Shaders

The asset system provides typed handles, asset stores, loading, processing,
metadata, and sources. Runtime systems refer to assets by `Handle<T>` and
`AssetId<T>` rather than direct file paths.

The shader module builds on assets. It supports WGSL, Slang source, SPIR-V, and
Slang IR, with import handling and runtime shader-module caching. Render modules
use shader handles and the shader cache when creating pipelines.

See [assets/quick.md](assets/quick.md) and [shader/quick.md](shader/quick.md).

## Tasks and Time

`epix.task` provides engine-wide concurrency primitives: task pools, awaitable
tasks, scoped parallelism, global compute/IO pools, async channels, and broadcast
channels.

`epix.time` provides real time, virtual game time, fixed-timestep schedules,
timers, stopwatches, and time-based run conditions. Simulations that need stable
steps should run in `FixedUpdate`; rendering and ordinary gameplay usually run
in `Update`.

See [task/quick.md](task/quick.md), [time/quick.md](time/quick.md), and
[time/fixed-timestep.md](time/fixed-timestep.md).

## Window and Input

Windows are ECS entities with a `Window` component. A backend plugin such as
GLFW or SFML owns the native event loop and keeps window components synchronized
with OS state.

Input is event-driven and stateful:

- Raw `KeyInput`, `MouseButtonInput`, `MouseMove`, and `MouseScroll` events
	preserve per-event detail.
- `ButtonInput<KeyCode>` and `ButtonInput<MouseButton>` provide held,
	just-pressed, and just-released state each frame.

See [window/quick.md](window/quick.md) and [input/quick.md](input/quick.md).

## Scene and Presentation Modules

Common scene-facing modules sit above the core ECS and renderer:

- `epix.transform`: local and global transforms.
- `epix.image`: image data and texture-oriented asset support.
- `epix.mesh`: mesh layouts, attributes, indices, and renderable mesh data.
- `epix.sprite`: 2D sprite components and bundles.
- `epix.text`: font assets, text components, shaping, and text rendering.
- `epix.render.imgui`: Dear ImGui integration for editor/debug UI.

These modules are intentionally smaller than the core renderer. They provide
ordinary components, resources, plugins, and render integration rather than a
monolithic scene system.

## Module Map

| Area | Main docs | Purpose |
| ---- | --------- | ------- |
| ECS | [ecs/quick.md](ecs/quick.md) | World, components, resources, systems, schedules, events, queries. |
| App | [app/quick.md](app/quick.md) | Plugins, runners, built-in schedules, states, extraction. |
| Task | [task/quick.md](task/quick.md) | Task pools, async work, channels. |
| Time | [time/quick.md](time/quick.md) | Frame time, fixed timestep, timers. |
| Window | [window/quick.md](window/quick.md) | ECS windows and OS backends. |
| Input | [input/quick.md](input/quick.md) | Keyboard/mouse events and button state. |
| Assets | [assets/quick.md](assets/quick.md) | Asset handles, stores, loaders, processors. |
| Shaders | [shader/quick.md](shader/quick.md) | Shader assets, imports, compilation, cache. |
| Render | [render/render/quick.md](render/render/quick.md) | WebGPU, render graph, pipelines, cameras. |
| Transform | [transform/quick.md](transform/quick.md) | Local/global transforms and hierarchy propagation. |
| Image | [image/quick.md](image/quick.md) | CPU images, loading, formats, and asset integration. |
| Mesh | [mesh/quick.md](mesh/quick.md) | Mesh assets, GPU upload, and 2D mesh rendering. |
| Sprite | [sprite/quick.md](sprite/quick.md) | Textured-quad components and Core2D rendering. |
| Text | [text/quick.md](text/quick.md) | Fonts, shaping, atlases, and Core2D text. |
| ImGui | [render/imgui/quick.md](render/imgui/quick.md) | Dear ImGui lifecycle, input capture, rendering. |

## Development Notes

- Follow [module_naming_convention.md](module_naming_convention.md) when adding
	modules or public symbols.
- Check [todo.md](todo.md) and module-specific TODO files before assuming a gap
	is accidental.
- Prefer existing module APIs over new ad hoc systems.
- When writing examples, start from the public API of the modules involved rather
	than copying an unrelated example wholesale.
