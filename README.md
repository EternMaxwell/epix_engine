# Epix Engine

Epix Engine is an experimental C++23, ECS-first game engine built around modules,
data-oriented scheduling, WebGPU rendering, and a plugin-driven application model.

It is currently a research and development engine. The codebase is moving quickly,
uses modern C++ module support heavily, and may require a recent compiler and CMake
combination to build successfully. Expect rough edges, API changes, and missing
features while the engine takes shape.

## What is Epix?

Epix is a modular game engine for people who want to work close to the engine
architecture while still getting a coherent set of game-development building
blocks: an ECS, schedules, plugins, async tasks, asset loading, shader management,
window/input integration, and a WebGPU-backed render stack.

The core style is familiar if you have used data-driven engines such as Bevy:
systems declare the data they read and write, schedules infer safe execution
order, and plugins compose the app out of focused modules.

## Design Goals

- **Data first**: entities, components, resources, events, and queries are the
	central vocabulary.
- **Modular by default**: use the modules you need, and keep engine systems
	split along clear feature boundaries.
- **Parallel where practical**: schedules and task pools are built for concurrent
	work without exposing every system to manual threading.
- **Renderer as a sub-app**: rendering runs in its own world, fed by extracted
	snapshots from the main simulation world.
- **Modern graphics path**: WebGPU, render graphs, render phases, shader assets,
	and asynchronous pipeline creation are first-class concepts.
- **Hackable engine internals**: the project favors readable C++ modules and
	explicit APIs over hiding the engine behind a black box.

## Current Feature Map

- **Core ECS**: `App`, `World`, entities, components, resources, bundles,
	events, labels, state machines, change detection, component hooks, and queries.
- **Schedules and systems**: built-in startup/update/exit schedules, run
	conditions, dependency-aware execution, and plugin lifecycle hooks.
- **Tasks**: thread pools, awaitable tasks, scoped parallel work, global pools,
	async channels, and broadcast channels.
- **Time**: real time, virtual time, fixed timestep schedules, timers,
	stopwatches, and time-based run conditions.
- **Window and input**: ECS windows, GLFW/SFML backends, window events,
	keyboard/mouse events, and per-frame button state resources.
- **Assets**: typed handles, asset stores, async loading, asset sources,
	hot-reload-capable watchers, processors, transformers, and sidecar metadata.
- **Shaders**: WGSL, Slang, SPIR-V, Slang IR, import composition, shader defs,
	and runtime shader-module caching.
- **Rendering**: WebGPU initialization, render sub-app, extraction, render graph,
	cameras, views, render phases, pipeline cache, render assets, and screenshots.
- **Scene primitives**: transforms, images, meshes, sprites, and text rendering.
- **Tools and extensions**: Dear ImGui integration plus experimental grid,
	GPU grid, falling-sand, and pixel-body modules.

See [documentation/architecture.md](documentation/architecture.md) for how the
pieces fit together.

## Getting Started

Start with [documentation/quick-start.md](documentation/quick-start.md). It walks
through the basic build flow and a tiny ECS program.

At a glance:

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DEPIX_ENABLE_TEST=ON -DEPIX_ENABLE_EXAMPLE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If your compiler or standard library does not support `import std` reliably yet,
configure with `-DEPIX_IMPORT_STD=OFF`.

## Minimal App

```cpp
import epix.core;

using namespace epix::core;

struct Position { float x, y; };
struct Velocity { float dx, dy; };

void spawn_entities(Commands commands) {
		commands.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
		commands.spawn(Position{10.0f, 5.0f}, Velocity{-1.0f, 0.0f});
}

void move_entities(Query<Item<Position&, const Velocity&>> query) {
		for (auto&& [pos, vel] : query.iter()) {
				pos.x += vel.dx;
				pos.y += vel.dy;
		}
}

int main() {
		App::create()
				.add_systems(Startup, into(spawn_entities))
				.add_systems(Update, into(move_entities))
				.run();
}
```

For a graphical app, look at the sprite, mesh, render, text, and window examples
under [epix_engine](epix_engine), then follow the module references below.

## Documentation

- [Quick Start](documentation/quick-start.md) - first build and first app.
- [Building](documentation/building.md) - toolchain, dependencies, options, and tests.
- [Architecture Overview](documentation/architecture.md) - ECS, schedules,
	render sub-app, extraction, assets, shaders, and module map.
- [Core](documentation/core/quick.md) - app, world, systems, queries, events,
	schedules, state, hierarchy, and change detection.
- [Tasks](documentation/tasks/quick.md) - task pools, awaitable tasks, scoped
	parallelism, global pools, and channels.
- [Time](documentation/time/quick.md) - real, virtual, and fixed-timestep time.
- [Window](documentation/window/quick.md) - ECS windows and GLFW/SFML backends.
- [Input](documentation/input/quick.md) - keyboard, mouse, events, and button state.
- [Assets](documentation/assets/quick.md) - handles, stores, loaders, sources,
	processors, metadata, and hot reload foundations.
- [Shaders](documentation/shader/quick.md) - WGSL, Slang, SPIR-V, imports, defs,
	and shader caching.
- [Render](documentation/render/render/quick.md) - WebGPU, render graph, cameras,
	views, phases, pipelines, and render assets.
- [ImGui](documentation/render/imgui/quick.md) - Dear ImGui integration.
- [Module Naming Convention](documentation/module_naming_convention.md) - naming
	rules for C++ modules and symbols.
- [Project TODO](documentation/todo.md) - known gaps and planned work.

## Building

The public build path is ordinary CMake. The project requires CMake 3.30 or newer,
C++23, and C++ module support. Ninja is recommended because C++ module scanning is
still sensitive to generator and compiler support.

```bash
cmake -S . -B build -G Ninja \
	-DEPIX_ENABLE_TEST=ON \
	-DEPIX_ENABLE_EXAMPLE=ON \
	-DEPIX_IMPORT_STD=ON

cmake --build build
ctest --test-dir build --output-on-failure
```

More details are in [documentation/building.md](documentation/building.md),
including cache options, submodules, WebGPU, Slang, and test notes.

## Contributing

There is no formal contributor guide yet. If you want to work on the engine,
start with the architecture overview, the module naming convention, and the
nearby tests for the module you are changing. Keep changes focused, document new
public APIs, and build or test the affected targets before sending a patch.

## License

Epix Engine is licensed under the GNU General Public License v3.0. See
[LICENSE.txt](LICENSE.txt) for the full license text.

Third-party libraries under [libs](libs) retain their own licenses.
