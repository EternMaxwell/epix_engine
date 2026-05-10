# Quick Start

This guide is the shortest path from a fresh checkout to a small Epix program.
Epix is still experimental, so the first successful build is mostly about having
a modern enough C++ modules toolchain.

## Prerequisites

- CMake 3.30 or newer.
- A C++23 compiler with working C++ module dependency scanning.
- Ninja or another generator with good module support.
- Git submodules initialized.
- Platform graphics requirements for WebGPU/Vulkan if you build or run render
	examples.

If `import std` causes toolchain issues, configure with `-DEPIX_IMPORT_STD=OFF`.

## 1. Prepare the Checkout

```bash
git submodule update --init --recursive
```

The repository vendors many dependencies under `libs/` and fetches a few others
during CMake configuration, including WebGPU support, Slang, and asio.

## 2. Configure, Build, and Test

```bash
cmake -S . -B build -G Ninja \
	-DEPIX_ENABLE_TEST=ON \
	-DEPIX_ENABLE_EXAMPLE=ON

cmake --build build
ctest --test-dir build --output-on-failure
```

For a build without `import std`:

```bash
cmake -S . -B build -G Ninja \
	-DEPIX_ENABLE_TEST=ON \
	-DEPIX_ENABLE_EXAMPLE=ON \
	-DEPIX_IMPORT_STD=OFF
```

See [building.md](building.md) for the full build guide and cache option list.

## 3. First ECS App

The smallest useful Epix app is a set of systems scheduled onto `Startup` and
`Update`.

```cpp
import epix.core;

using namespace epix::core;

struct Position {
		float x = 0.0f;
		float y = 0.0f;
};

struct Velocity {
		float dx = 0.0f;
		float dy = 0.0f;
};

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

The important pieces are:

- `App::create()` installs the default app loop and main schedule plugin.
- `Commands` queues entity and resource mutations safely from systems.
- `Query<Item<...>>` declares the component access pattern for a system.
- `into(...)` wraps ordinary functions into systems that can be scheduled.

For a deeper tour of these types, continue with [core/quick.md](core/quick.md).

## 4. Open a Window

Windowing is built from a backend-agnostic `WindowPlugin` plus a backend plugin.
Use GLFW unless you have a reason to use SFML.

```cpp
import epix.core;
import epix.window;
import epix.glfw.core;

int main() {
	using namespace epix;

	core::App app = core::App::create();

	app.add_plugins(core::TaskPoolPlugin{})
	   .add_plugins(window::WindowPlugin{
		   .primary_window = window::Window{
			   .title = "Epix Window",
			   .size = {1280, 720},
		   },
		   .exit_condition = window::ExitCondition::OnPrimaryClosed,
	   })
	   .add_plugins(glfw::GLFWPlugin{});

	app.run();
}
```

See [window/quick.md](window/quick.md) for the backend details and event list.

## 5. Move Toward a Rendered App

A rendered 2D app typically combines these plugins:

- `TaskPoolPlugin` for background engine work.
- `WindowPlugin` and `GLFWPlugin` for the OS event loop.
- `GLFWRenderPlugin` and `RenderPlugin` for WebGPU surfaces and the render sub-app.
- `TransformPlugin` for local/global transforms.
- `CoreGraphPlugin` for the built-in 2D render graph.
- `SpritePlugin`, `Mesh`, or `Text` modules depending on what you draw.

The compact real example to study first is
[../epix_engine/sprite/examples/basic.cpp](../epix_engine/sprite/examples/basic.cpp).
It creates a window, render app, camera, in-memory texture, and a single sprite.

From there, read:

- [render/render/quick.md](render/render/quick.md) for render graph, cameras,
  pipeline cache, and render assets.
- [window/quick.md](window/quick.md) for windows and backend runners.
- [input/quick.md](input/quick.md) for keyboard and mouse state.
- [assets/quick.md](assets/quick.md) for loading and storing assets.
- [shader/quick.md](shader/quick.md) for WGSL, Slang, SPIR-V, and shader cache.

## 6. Development Loop

For docs and small core changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

For renderer or example changes, also run the closest example executable from
your build tree and watch the log output. Use `SPDLOG_LEVEL=debug` or
`SPDLOG_LEVEL=trace` when you need more detail.

## Next Steps

- Read [architecture.md](architecture.md) to understand the engine shape.
- Read [building.md](building.md) if CMake or compiler setup fails.
- Browse module quick references under [documentation](.) as you need features.
- Check [todo.md](todo.md) before assuming a rough edge is accidental.
