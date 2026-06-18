# Building Epix Engine

Epix currently targets a modern C++23 modules toolchain. Build failures are often
toolchain issues rather than engine issues, so start by checking CMake, compiler,
module scanning, and `import std` support.

## Requirements

- **CMake 3.30+**: required for the project's module and `import std` setup.
- **C++23 compiler**: C++ modules support is mandatory.
- **Module dependency scanner**: your compiler and generator must support C++
	module scanning well enough for CMake to order builds correctly.
- **Ninja or equivalent**: Ninja is recommended for local development.
- **Git submodules**: most dependencies live under `libs/`.
- **Graphics stack**: render examples need WebGPU/Vulkan support on the host.

The engine builds with `EPIX_CXX_MODULE=ON`; disabling it is not supported.

## Dependencies

The repository uses two dependency paths:

- **Vendored submodules** under `libs/`, including GLFW, SFML, spdlog,
	FreeType, HarfBuzz, ImGui, Box2D, GoogleTest, Tracy, stb, uuid, glm,
	taskflow, stdexec, and related libraries.
- **CMake-fetched/generated support** for WebGPU, Slang, asio, and the generated
	WebGPU C++ module wrapper.

Initialize submodules before configuring:

```bash
git submodule update --init --recursive
```

## Configure

Recommended default development configure:

```bash
cmake -S . -B build -G Ninja \
	-DEPIX_ENABLE_TEST=ON \
	-DEPIX_ENABLE_EXAMPLE=ON
```

If your standard library modules support is not ready, turn off `import std`:

```bash
cmake -S . -B build -G Ninja \
	-DEPIX_ENABLE_TEST=ON \
	-DEPIX_ENABLE_EXAMPLE=ON \
	-DEPIX_IMPORT_STD=OFF
```

For release builds with single-config generators:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

## Build

```bash
cmake --build build
```

To build a specific target, ask CMake for available targets for your generator or
inspect the configure output, then pass `--target`:

```bash
cmake --build build --target epix_core
```

## Test

Tests are enabled by default through `EPIX_ENABLE_TEST=ON`.

```bash
ctest --test-dir build --output-on-failure
```

Engine tests run with the repository root as the working directory so paths such
as `assets/...` resolve consistently.

## Important Cache Options

| Option | Default | Notes |
| ------ | ------- | ----- |
| `EPIX_ENABLE_TEST` | `ON` | Builds GoogleTest-based engine tests. |
| `EPIX_ENABLE_EXAMPLE` | `ON` | Builds example executables from module example folders. |
| `EPIX_ENABLE_TRACY` | `OFF` | Enables Tracy profiling definitions. |
| `EPIX_USE_VOLK` | `ON` | Enables Volk-related compile definitions for Vulkan loading. |
| `EPIX_CXX_MODULE` | `ON` | Mandatory; the project errors if this is disabled. |
| `EPIX_IMPORT_STD` | `ON` | Enables `import std`; turn off for toolchains where standard library modules are unstable. |
| `EPIX_WGPU_LINK_TYPE` | `STATIC` | WebGPU native link mode; accepted values are `STATIC` and `SHARED`. |
| `EPIX_WGPU_NATIVE_VERSION` | `v25.0.2.2` | wgpu-native version fetched during configure. |
| `EPIX_WGPU_GENERATE_ON_CONFIGURE` | `ON` | Generates the WebGPU C++ module wrapper during configure when the generator is available. |

Use ordinary CMake cache syntax to override options:

```bash
cmake -S . -B build -G Ninja -DEPIX_IMPORT_STD=OFF -DEPIX_ENABLE_EXAMPLE=OFF
```

## WebGPU and Slang

The render stack depends on wgpu-native and a generated C++ module wrapper named
`webgpu`. During configuration the build scripts:

1. Detect the host OS and architecture.
2. Fetch the configured wgpu-native package.
3. Generate the `webgpu.cppm` wrapper into the build tree when configured to do so.
4. Create the `webgpu` target used by render modules.

Slang is fetched separately for shader compilation and processing. Network access
may be required the first time you configure a clean build directory.

## Examples

When `EPIX_ENABLE_EXAMPLE=ON`, CMake creates example executables from each
module's `examples/` directory. Examples are intentionally varied because they
exercise different engine layers. Good starting points are:

- [../epix_engine/window/examples/glfw/module_test.cpp](../epix_engine/window/examples/glfw/module_test.cpp)
	for a simple GLFW window app.
- [../epix_engine/render/examples/render_plugin.cpp](../epix_engine/render/examples/render_plugin.cpp)
	for the render plugin path.
- [../epix_engine/sprite/examples/basic.cpp](../epix_engine/sprite/examples/basic.cpp)
	for a small 2D sprite app.
- [../epix_engine/text/examples/text_interactive.cpp](../epix_engine/text/examples/text_interactive.cpp)
	for text rendering.

Run examples from the build tree produced by your generator. For debugging,
increase logging verbosity:

```bash
SPDLOG_LEVEL=debug ./path/to/example_executable
```

## Common Problems

### CMake cannot enable `CxxImportStd`

Your CMake/compiler/standard-library combination likely does not support standard
library modules in the way CMake expects. Reconfigure with:

```bash
cmake -S . -B build -G Ninja -DEPIX_IMPORT_STD=OFF
```

### Module dependency scanning fails

Use a newer compiler, verify the compiler's module scanner is installed and on
the path, and prefer Ninja. C++ module support is still evolving across compilers.

### Render examples fail to create an adapter

Check Vulkan/WebGPU driver support, GPU availability, and validation layer setup.
Set `EPIX_PRINT_WEBGPU_ADAPTERS=1` when running a render example to log available
adapters.

### First configure downloads dependencies

This is expected for wgpu-native, Slang, and asio. Reusing the same build tree or
CMake download cache should make later configures faster.