# Building epix_engine with WebGPU Backend

This guide covers building epix_engine with the WebGPU rendering backend and C++20 module support.

## Prerequisites

### Required
- CMake 3.30 or later (for C++20 module support)
- C++23 compatible compiler:
  - **MSVC 2022 (17.5+) on Windows** - Recommended for full module support
  - **GCC 13+** on Linux - Note: Module scanning support is limited; use header mode
  - **Clang 16+** - Experimental module support
- Python 3.10+ (for WebGPU wrapper generation)
- Git (for submodule management)
- **Ninja build system** (required for C++20 modules)

### Platform-Specific
- **Windows**: Visual Studio 2022 with C++ workload
- **Linux**: X11 or Wayland development libraries
- **macOS**: Xcode with Command Line Tools

## Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/EternMaxwell/epix_engine.git
cd epix_engine
git submodule update --init --recursive
```

### 2. Configure CMake with WebGPU

```bash
mkdir build && cd build

# Basic configuration with WebGPU
cmake .. -DEPX_USE_WEBGPU=ON

# Advanced configuration
cmake .. \
  -DEPX_USE_WEBGPU=ON \
  -DEPX_WGPU_USE_MODULE=ON \
  -DEPX_WGPU_NATIVE_VERSION="v24.0.3.1" \
  -DEPX_WGPU_LINK_TYPE=SHARED \
  -DEPX_BUILD_EXAMPLES=ON
```

### 3. Build

```bash
# Build the project
cmake --build . --config Release

# Or on Windows with MSVC
cmake --build . --config Release -- /m
```

### 4. Run Examples

```bash
# Run the simple WebGPU example
./examples/webgpu_simple/webgpu_simple

# On Windows
.\examples\webgpu_simple\Release\webgpu_simple.exe
```

## CMake Options

### WebGPU Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `EPX_USE_WEBGPU` | `ON` | Enable WebGPU backend integration |
| `EPX_WGPU_USE_MODULE` | `ON` | Generate and use C++20 module for WebGPU |
| `EPX_WGPU_NATIVE_VERSION` | `"v24.0.3.1"` | Version of wgpu-native to fetch |
| `EPX_WGPU_LINK_TYPE` | `"SHARED"` | Link type: `SHARED` or `STATIC` |
| `EPX_WGPU_GENERATE_ON_CONFIGURE` | `ON` | Generate wrapper during configure vs build |
| `EPX_WGPU_MODULE_NAME` | `"webgpu"` | Name of the generated C++20 module |
| `EPX_BUILD_EXAMPLES` | `ON` | Build WebGPU example applications |

### Engine Options

| Option | Default | Description |
|--------|---------|-------------|
| `EPIX_CXX_MODULE` | `ON` | Enable C++20 modules for engine |
| `EPIX_IMPORT_STD` | `ON` | Enable C++ `import std` |
| `EPIX_ENABLE_TRACY` | `OFF` | Enable Tracy profiler |
| `EPIX_ENABLE_TEST` | `ON` | Enable test builds |

## Using WebGPU in Your Code

### With C++20 Modules (Recommended)

```cpp
import std;
import webgpu;

int main() {
    // RAII handles are the default
    wgpu::InstanceDescriptor desc = wgpu::Default;
    wgpu::Instance instance = wgpu::createInstance(desc);
    
    // Automatically cleaned up when going out of scope
    return 0;
}
```

### With Headers (Legacy)

```cpp
#include <iostream>
#include "webgpu.hpp"

int main() {
    wgpu::InstanceDescriptor desc = wgpu::Default;
    wgpu::Instance instance = wgpu::createInstance(desc);
    return 0;
}
```

### RAII vs Raw Handles

By default, the module exports RAII handles in the `wgpu::` namespace:

```cpp
import webgpu;

// These are RAII handles (automatically managed)
wgpu::Device device;
wgpu::Buffer buffer;
wgpu::Texture texture;

// Raw handles are still available via wgpu::raw::
wgpu::raw::Device rawDevice;  // No automatic cleanup
```

## Platform-Specific Notes

### Windows (MSVC)

- Ensure Visual Studio 2022 17.5+ is installed
- Use "x64 Native Tools Command Prompt for VS 2022"
- Module compilation generates `.ifc` files in the build directory
- The wgpu-native DLL is automatically copied next to executables

```bash
# Configure for MSVC
cmake .. -G "Visual Studio 17 2022" -A x64 -DEPX_USE_WEBGPU=ON

# Build
cmake --build . --config Release
```

### Linux (GCC/Clang)

- Install development packages:
  ```bash
  # Ubuntu/Debian
  sudo apt install build-essential cmake python3 ninja-build libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
  
  # Fedora
  sudo dnf install gcc-c++ cmake python3 ninja-build libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
  ```

- **Important**: GCC module scanning support is experimental. Use Ninja generator:
  ```bash
  cmake .. -G Ninja -DEPX_USE_WEBGPU=ON -DGLFW_BUILD_WAYLAND=OFF
  ```

- **Note**: If you encounter module compilation errors with GCC, you can use header-only mode:
  ```bash
  cmake .. -G Ninja -DEPX_USE_WEBGPU=ON -DEPX_WGPU_USE_MODULE=OFF -DGLFW_BUILD_WAYLAND=OFF
  ```

- Disable Wayland if you encounter issues:
  ```bash
  cmake .. -G Ninja -DEPX_USE_WEBGPU=ON -DGLFW_BUILD_WAYLAND=OFF
  ```

### macOS

- Install Xcode Command Line Tools:
  ```bash
  xcode-select --install
  ```

- Configure and build:
  ```bash
  cmake .. -DEPX_USE_WEBGPU=ON
  cmake --build . --config Release
  ```

## Troubleshooting

### "Compiler does not provide a way to discover import graph dependencies" (GCC)

GCC's C++20 module scanning support is still experimental. Solutions:

1. **Use MSVC on Windows** (recommended for full module support)
2. **Use header-only mode** with GCC:
   ```bash
   cmake .. -G Ninja -DEPX_USE_WEBGPU=ON -DEPX_WGPU_USE_MODULE=OFF
   ```
3. **Disable engine module support** (if only using WebGPU headers):
   ```bash
   cmake .. -G Ninja -DEPX_USE_WEBGPU=ON -DEPIX_CXX_MODULE=OFF
   ```

### "Python3 not found"

Install Python 3.10 or later and ensure it's in your PATH:
```bash
# Linux
sudo apt install python3

# macOS
brew install python3

# Windows
# Download from python.org
```

### "Failed to fetch wgpu-native"

Check your internet connection and firewall settings. The build system downloads precompiled binaries from:
```
https://github.com/gfx-rs/wgpu-native/releases/
```

### Module Compilation Errors (MSVC)

Ensure you're using MSVC 17.5+ and CMake 3.30+:
```bash
cmake --version
cl.exe  # Check Visual Studio version
```

### "wayland-scanner not found" (Linux)

Either install wayland-scanner or disable Wayland support:
```bash
# Install wayland-scanner
sudo apt install wayland-protocols libwayland-dev

# Or disable Wayland
cmake .. -DGLFW_BUILD_WAYLAND=OFF
```

## Advanced Configuration

### Using a Specific wgpu-native Version

```bash
cmake .. -DEPX_WGPU_NATIVE_VERSION="v23.0.0"
```

Available versions: https://github.com/gfx-rs/wgpu-native/releases

### Static Linking

```bash
cmake .. -DEPX_USE_WEBGPU=ON -DEPX_WGPU_LINK_TYPE=STATIC
```

Note: Static linking includes additional system libraries on each platform.

### Disabling Module Generation

```bash
cmake .. -DEPX_USE_WEBGPU=ON -DEPX_WGPU_USE_MODULE=OFF
```

This uses header-only mode without C++20 modules.

### Testing WebGPU Generation Only

To verify WebGPU setup without building the entire engine:

```bash
# Configure and generate WebGPU wrappers
cmake .. -G Ninja -DEPX_USE_WEBGPU=ON -DGLFW_BUILD_WAYLAND=OFF

# Check generated files
ls -la build/generated/webgpu/
# Should show: webgpu.hpp, webgpu-raii.hpp, webgpu.cppm
```

The WebGPU wrapper generation happens during CMake configuration, so you can verify the infrastructure works without completing the full build.

### Custom WebGPU-Cpp Repository

```bash
cmake .. \
  -DEPX_WEBGPU_CPP_REPO="https://github.com/YourFork/WebGPU-Cpp.git" \
  -DEPX_WEBGPU_CPP_TAG="your-branch"
```

## CI/Automated Builds

### GitHub Actions Example

```yaml
name: Build with WebGPU

on: [push, pull_request]

jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    
    steps:
    - uses: actions/checkout@v3
      with:
        submodules: recursive
    
    - name: Setup Python
      uses: actions/setup-python@v4
      with:
        python-version: '3.11'
    
    - name: Configure
      run: |
        cmake -B build -DEPX_USE_WEBGPU=ON -DGLFW_BUILD_WAYLAND=OFF
    
    - name: Build
      run: |
        cmake --build build --config Release
    
    - name: Test
      run: |
        cd build
        ctest -C Release --output-on-failure
```

## Support

For issues and questions:
- GitHub Issues: https://github.com/EternMaxwell/epix_engine/issues
- WebGPU-Cpp: https://github.com/eliemichel/WebGPU-Cpp
- wgpu-native: https://github.com/gfx-rs/wgpu-native

## License

See LICENSE.txt for license information.
