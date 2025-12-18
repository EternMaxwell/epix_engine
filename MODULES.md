# C++20 Modules Support in Epix Engine

## Overview

This document describes the C++20 modules implementation in Epix Engine.

## Build Configuration

### Enabling Modules

To build with C++20 modules enabled, use the CMake option:

```bash
cmake -DEPIX_ENABLE_MODULES=ON ..
```

Or use one of the module-enabled presets:

```bash
cmake --preset=debug-msvc-modules
cmake --preset=release-clang-modules
cmake --preset=debug-gcc-modules
```

### Compiler Requirements

- **MSVC**: Visual Studio 2022 (17.0) or later with `/std:c++20`
- **Clang**: Clang 16.0 or later with `-fmodules`
- **GCC**: GCC 11.0 or later with `-fmodules-ts`

## Module Structure

### Available Modules

The following modules are available when `EPIX_ENABLE_MODULES=ON`:

1. **epix.core** - ECS core functionality (World, Entities, Systems, Events)
2. **epix.input** - Input handling
3. **epix.assets** - Asset loading and management
4. **epix.window** - Window abstraction
5. **epix.transform** - Transform components
6. **epix.image** - Image data structures
7. **epix.render** - Rendering system
8. **epix.sprite** - 2D sprite rendering
9. **epix.text** - Text rendering

### Module Dependencies

```
epix.core (base)
├── epix.input
├── epix.assets
├── epix.transform
├── epix.image
└── epix.window
    └── epix.render
        ├── epix.sprite
        └── epix.text
```

## Implementation Approach

### Hybrid Module/Header Design

Due to the complexity of the codebase with heavy use of:
- Third-party headers (nvrhi, glm, entt, etc.)
- Template-heavy code
- Macro-based code

The modules implementation uses a **hybrid approach**:

1. **When `EPIX_ENABLE_MODULES=ON`**: Module interface units re-export existing headers
2. **When `EPIX_ENABLE_MODULES=OFF`**: Traditional header-based build (default)

This ensures:
- No breaking changes to the public API
- Full backward compatibility
- Third-party libraries remain as headers
- Gradual migration path to true modules

### Using Modules in Your Code

When modules are enabled, you can use them in your code:

```cpp
// Module-based import (when EPIX_ENABLE_MODULES=ON)
import epix.core;
import epix.render;

// Traditional include (always works)
#include <epix/core.hpp>
#include <epix/render.hpp>
```

Both approaches work and provide the same API.

## Migration Status

### Converted Modules

| Module | Status | Notes |
|--------|--------|-------|
| epix.core | Planned | ECS core, templates, macros |
| epix.input | Planned | Simple module candidate |
| epix.assets | Planned | Asset system |
| epix.window | Planned | Window abstraction |
| epix.transform | Planned | Transform components |
| epix.image | Planned | Image handling |
| epix.render | Planned | Rendering system |
| epix.sprite | Planned | 2D sprites |
| epix.text | Planned | Text rendering |

### Not Converted (Remain as Headers)

Third-party libraries in `libs/` remain as traditional headers:
- entt, glfw, spdlog, glm, freetype, vulkan-headers
- volk, imgui, box2d, tracy, earcut, stb
- uuid, nvrhi, spirv-cross, utfcpp, harfbuzz

## Known Limitations

1. **Template Instantiation**: Some template-heavy code may still require header includes
2. **Macro Compatibility**: Macros cannot be exported from modules
3. **Third-party Dependencies**: External libraries remain as headers
4. **Compiler Support**: Module support varies across compilers and versions
5. **Build Time**: Initial module builds may be slower due to BMI compilation

## Testing

Both module and header builds are tested:

```bash
# Test without modules (default)
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
ctest

# Test with modules
cmake -DCMAKE_BUILD_TYPE=Debug -DEPIX_ENABLE_MODULES=ON ..
cmake --build .
ctest
```

## Troubleshooting

### Module Build Fails

1. Verify compiler version meets requirements
2. Check that all submodules are initialized
3. Try building without modules first to ensure base build works
4. Check compiler-specific module flags in `CMakeLists.txt`

### Link Errors

If you encounter link errors when using modules, ensure you're linking against the same targets:

```cmake
target_link_libraries(your_target PRIVATE epix_core epix_render)
```

## Future Work

- [ ] Complete module interface unit implementations
- [ ] Optimize module build times
- [ ] Add CI testing for module builds
- [ ] Profile compilation improvements
- [ ] Explore module partitions for large modules
