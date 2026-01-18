# epix_engine

A modern C++23 game engine with WebGPU rendering backend and full C++20 module support.

## Features

- **WebGPU Rendering**: Cross-platform graphics using the modern WebGPU API
- **C++20 Modules**: First-class module support for faster compilation and better encapsulation
- **RAII by Default**: WebGPU handles automatically managed for safe resource cleanup
- **Cross-Platform**: Windows, Linux, and macOS support
- **Modern C++**: Built with C++23 standard features
- **Configurable**: Choose wgpu-native versions and linking options via CMake

## Quick Start

### Prerequisites

- CMake 3.30+
- C++23 compiler (MSVC 2022, GCC 13+, or Clang 16+)
- Python 3.10+ (for code generation)

### Build

```bash
# Clone with submodules
git clone https://github.com/EternMaxwell/epix_engine.git
cd epix_engine
git submodule update --init --recursive

# Configure and build
mkdir build && cd build
cmake .. -DEPX_USE_WEBGPU=ON
cmake --build . --config Release

# Run example
./examples/webgpu_simple/webgpu_simple
```

See [BUILD.md](BUILD.md) for detailed build instructions.

## WebGPU Backend

The engine uses **wgpu-native** (Rust implementation of WebGPU) with C++ bindings generated from [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp).

### Key Features

- **Precompiled Binaries**: No Rust toolchain required - automatically downloads wgpu-native releases
- **Module Interface**: Generated C++20 module for clean imports
- **RAII Handles**: Default handles are RAII-wrapped for automatic cleanup
- **Version Control**: Choose specific wgpu-native versions via CMake options

### Usage Example

```cpp
import std;
import webgpu;

int main() {
    // Create instance with RAII handle
    wgpu::InstanceDescriptor desc = wgpu::Default;
    wgpu::Instance instance = wgpu::createInstance(desc);
    
    // Request adapter
    wgpu::RequestAdapterOptions opts = wgpu::Default;
    wgpu::Adapter adapter = nullptr;
    
    instance.requestAdapter(opts, [&](auto status, auto adpt, auto msg) {
        if (status == wgpu::RequestAdapterStatus::Success) {
            adapter = std::move(adpt);
        }
    });
    
    // Resources automatically cleaned up
    return 0;
}
```

## Project Structure

```
epix_engine/
├── cmake/                  # CMake modules
│   ├── FetchWgpuNative.cmake      # Fetch wgpu-native binaries
│   ├── WebGPUCppGenerator.cmake   # Generate C++ wrappers
│   ├── WebGPU.cmake              # Main WebGPU integration
│   └── templates/                # Module templates
├── epix_engine/            # Engine source code
│   ├── core/              # Core engine systems
│   ├── render/            # Rendering subsystem
│   └── window/            # Window management
├── examples/              # Example applications
│   └── webgpu_simple/     # Simple WebGPU demo
├── libs/                  # Third-party libraries
└── BUILD.md              # Detailed build guide
```

## CMake Options

### WebGPU Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `EPX_USE_WEBGPU` | `ON` | Enable WebGPU backend |
| `EPX_WGPU_USE_MODULE` | `ON` | Generate C++20 module |
| `EPX_WGPU_NATIVE_VERSION` | `"v24.0.3.1"` | wgpu-native version |
| `EPX_WGPU_LINK_TYPE` | `"SHARED"` | `SHARED` or `STATIC` |

See [BUILD.md](BUILD.md) for all options.

## Architecture

### Module System

The engine is built with C++20 modules for:
- Faster compilation (no header parsing overhead)
- Better encapsulation (explicit exports)
- Cleaner dependencies (explicit imports)

### WebGPU Integration

1. **CMake Configuration**: Downloads wgpu-native binaries for your platform
2. **Code Generation**: Runs WebGPU-Cpp generator to create C++ bindings
3. **Module Creation**: Wraps generated code in a C++20 module interface
4. **RAII Wrapping**: Makes RAII handles the default in `wgpu::` namespace

### Resource Management

All WebGPU handles use RAII by default:

```cpp
import webgpu;

// RAII handles (automatic cleanup)
wgpu::Device device;        // Cleaned up when scope ends
wgpu::Buffer buffer;        // Automatically released
wgpu::Texture texture;      // Memory-safe

// Raw handles still available if needed
wgpu::raw::Device rawDevice;  // Manual management
```

## Examples

See the `examples/` directory for:
- `webgpu_simple/` - Basic WebGPU initialization and adapter queries

More examples coming soon!

## Contributing

Contributions are welcome! Please see the issue tracker for areas needing help.

## Documentation

- [BUILD.md](BUILD.md) - Comprehensive build guide
- [examples/](examples/) - Example code and tutorials
- [WebGPU Spec](https://www.w3.org/TR/webgpu/) - Official WebGPU specification
- [wgpu-native](https://github.com/gfx-rs/wgpu-native) - Native WebGPU implementation

## Dependencies

### Core
- [wgpu-native](https://github.com/gfx-rs/wgpu-native) - WebGPU implementation
- [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp) - C++ bindings generator
- [GLFW](https://www.glfw.org/) - Window and input handling
- [GLM](https://github.com/g-truc/glm) - Mathematics library

### Optional
- [Tracy](https://github.com/wolfpld/tracy) - Profiler (if `EPIX_ENABLE_TRACY=ON`)
- [GoogleTest](https://github.com/google/googletest) - Testing framework

## License

See [LICENSE.txt](LICENSE.txt) for license information.

## Acknowledgments

- [Elie Michel](https://github.com/eliemichel) - WebGPU-Cpp generator and Learn WebGPU
- [gfx-rs team](https://github.com/gfx-rs) - wgpu-native implementation
- WebGPU Community Group - WebGPU specification

## Status

This project is under active development. The WebGPU backend integration is experimental but functional.

Current status:
- ✅ WebGPU infrastructure and CMake integration
- ✅ C++20 module generation and compilation
- ✅ RAII-by-default handle management
- ✅ Basic example application
- 🚧 Full rendering pipeline integration
- 🚧 Platform-specific testing (Windows/Linux/macOS)
- 📝 Comprehensive documentation

## Support

For questions and issues:
- [GitHub Issues](https://github.com/EternMaxwell/epix_engine/issues)
- [WebGPU-Cpp Documentation](https://eliemichel.github.io/LearnWebGPU/)
