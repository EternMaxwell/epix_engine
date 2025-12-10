# C++20 Modules Support in Epix Engine

This document describes the C++20 modules support in the Epix Engine.

## Overview

The Epix Engine now includes experimental C++20 module interfaces alongside the traditional header-based API. This allows modern C++ projects to benefit from:

- Faster compilation times (after initial build)
- Better isolation and encapsulation
- Cleaner dependency management
- Reduced macro pollution

## Requirements

To use C++20 modules with Epix Engine, you need:

- CMake 3.28 or later
- One of the following compilers:
  - MSVC 19.29+ (Visual Studio 2019 16.10+)
  - Clang 16+
  - GCC 14+

## Enabling Module Support

To build with C++20 modules support, configure your CMake build with:

```bash
cmake -B build -DEPIX_USE_MODULES=ON
```

This will create additional library targets with the `_modules` suffix that can be used in your project.

## Module Structure

The engine provides the following modules:

| Module | Description | Dependencies |
|--------|-------------|--------------|
| `epix.core` | Core ECS functionality | - |
| `epix.core.api` | API macros and wrapper types | - |
| `epix.core.meta` | Compile-time type reflection | - |
| `epix.core.tick` | Change detection system | - |
| `epix.core.type_system` | Runtime type information | `epix.core.api`, `epix.core.meta` |
| `epix.core.entities` | Entity management | `epix.core.api` |
| `epix.input` | Input handling | `epix.core` |
| `epix.assets` | Asset management | `epix.core` |
| `epix.transform` | Transform components | `epix.core` |
| `epix.window` | Window management | `epix.core`, `epix.input`, `epix.assets` |
| `epix.image` | Image handling | `epix.core`, `epix.assets` |
| `epix.render` | Rendering system | `epix.core`, `epix.assets`, `epix.window`, `epix.image` |
| `epix.sprite` | Sprite rendering | `epix.core`, `epix.render`, `epix.transform`, `epix.image` |

## Usage

### Using Modules (C++20)

```cpp
import epix.core;
import epix.render;
import epix.sprite;

int main() {
    auto app = epix::core::App::create();
    app.add_plugins(epix::render::RenderPlugin{});
    app.add_plugins(epix::sprite::SpritePlugin{});
    app.run();
    return 0;
}
```

### Using Headers (Traditional)

```cpp
#include <epix/core.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>

int main() {
    auto app = epix::App::create();
    app.add_plugins(epix::render::RenderPlugin{});
    app.add_plugins(epix::sprite::SpritePlugin{});
    app.run();
    return 0;
}
```

### CMake Configuration

For module-based builds:

```cmake
target_link_libraries(my_app PRIVATE
    epix_core_modules
    epix_render_modules
    epix_sprite_modules
)
```

For traditional header-based builds:

```cmake
target_link_libraries(my_app PRIVATE
    epix_core
    epix_render
    epix_sprite
)
```

## Feature Test Macros

The engine provides several feature test macros for compatibility:

| Macro | Description |
|-------|-------------|
| `EPIX_HAS_CPP20_MODULES` | Set to 1 if C++20 modules are supported |
| `EPIX_HAS_STD_MODULES` | Set to 1 if standard library modules are available |
| `EPIX_MODULES_ENABLED` | Set to 1 if modules are enabled for this build |
| `EPIX_HAS_DEDUCING_THIS` | Set to 1 if C++23 deducing this is supported |
| `EPIX_HAS_RANGES_TO` | Set to 1 if `std::ranges::to` is available |
| `EPIX_HAS_VIEWS_ENUMERATE` | Set to 1 if `std::views::enumerate` is available |
| `EPIX_HAS_STD_EXPECTED` | Set to 1 if `std::expected` is available |

## Compiler-Specific Notes

### MSVC

MSVC has the most mature C++20 modules support. Enable with `/std:c++20` or `/std:c++latest`.

### Clang

Clang requires `-fmodules` or `-fmodules-ts` flags. Module support is improving but may have some limitations.

### GCC

GCC 14+ provides experimental modules support with `-fmodules-ts`. Some features may not work as expected.

## C++23 Feature Fallbacks

The engine uses some C++23 features that may not be available on all compilers. When these features are not available, the engine provides fallback implementations:

- `std::ranges::to` - Provided via `epix::compat::to` when not available
- `std::views::enumerate` - Provided via `epix::compat::enumerate` when not available

The fallbacks are automatically used when the corresponding feature test macros indicate the feature is not available.

## Known Limitations

1. **Module interface stability**: Module interfaces may change between versions. We recommend using header-based builds for production code until the module system matures.

2. **Build system support**: Not all build systems fully support C++20 modules. CMake 3.28+ has the best support.

3. **Third-party dependencies**: Some third-party libraries used by the engine (like NVRHI, GLM) do not provide module interfaces, so they are included via the global module fragment.

4. **Incremental builds**: Module builds may require more careful dependency management for correct incremental builds.

## Migration Guide

To migrate from header-based to module-based builds:

1. Ensure your compiler supports C++20 modules
2. Update your CMakeLists.txt to link against `*_modules` targets
3. Replace `#include` directives with `import` statements
4. Update any code that relies on macro definitions (macros are not exported from modules)

## Troubleshooting

### "Module not found" errors

Ensure that:
- `EPIX_USE_MODULES` is enabled
- CMake 3.28+ is being used
- Your compiler supports modules

### Linker errors

Module libraries must be linked in the correct order. Ensure dependencies are listed before dependent modules.

### Build performance

Initial builds with modules may be slower. Subsequent incremental builds should be faster.
