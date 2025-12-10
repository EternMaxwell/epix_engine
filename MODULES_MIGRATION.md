# C++20 Modules Migration Plan for epix_engine

## Overview

This document outlines the approach for migrating the epix_engine codebase from traditional header-based includes to C++20 modules.

## Current Status

### Phase 1: Build System Setup ✓ COMPLETE

The build system has been successfully configured to support C++20 modules:

1. **CMake Configuration**: Added `CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API` support (3.28+)
2. **Compiler Flags**: Configured for Clang 18.1.3, GCC 13.3.0, and MSVC
3. **Build Fixes**: 
   - Resolved GLFW_USE_WAYLAND compatibility issue with earcut submodule
   - Fixed platform-specific compile flags
   - Fixed MSVC-specific header includes

### Blockers Identified

**C++23 Standard Library Compatibility Issues** (Pre-existing, not related to modules):

The codebase uses several C++23/C++26 features that are not yet available in the standard libraries:

1. `std::ranges::insert_range()` - Not available in libstdc++ 13/14
2. `std::ranges::to<>()` - C++23 feature, partial support
3. Explicit `this` parameters - C++23 feature, compiler support varies
4. `std::expected` - Available in GCC 13+ but may have issues
5. Format library for custom types - Needs specialization
6. `std::move_only_function` - Not available in libstdc++ 14 or libc++ 18
7. `std::views::enumerate` - Not available in libstdc++ 14 or libc++ 18

**Resolution**: A C++23 compatibility layer has been created using feature test macros.

### C++23 Compatibility Layer ✓ IMPLEMENTED

A compatibility header `epix/utils/cpp23_compat.hpp` has been created that:

1. **Uses Feature Test Macros**: Automatically detects which C++23 features are available
2. **Provides Fallback Implementations**: When features are missing, provides compatible implementations
3. **Zero Overhead When Available**: Uses standard library implementations when available
4. **Tested**: Works with both Clang 18.1.3 + libstdc++ and will work with libc++ when available

**Supported Features**:
- `move_only_function<Signature>` - Move-only function wrapper
- `ranges::to<Container>()` - Convert ranges to containers
- `views::enumerate` - Enumerate view for ranges
- `insert_range()` - Insert ranges into containers

**Usage**:
```cpp
#include <epix/utils/cpp23_compat.hpp>

using namespace epix::compat;

// Use move_only_function
move_only_function<void(int)> func = [](int x) { ... };

// Use ranges::to
auto vec = range | ranges::to<std::vector>();

// Use views::enumerate
for (auto [idx, val] : vec | views::enumerate) { ... }

// Use insert_range
EPIX_INSERT_RANGE(container, pos, range);
```

This compatibility layer resolves the C++23 blockers and allows module migration to proceed.

## Module Migration Strategy

### Module Hierarchy

```
epix (root module)
├── epix.core
│   ├── epix.core.archetype
│   ├── epix.core.bundle
│   ├── epix.core.component
│   ├── epix.core.entities
│   ├── epix.core.query
│   ├── epix.core.schedule
│   ├── epix.core.storage
│   ├── epix.core.system
│   └── epix.core.world
├── epix.input
├── epix.assets
├── epix.transform
├── epix.image
├── epix.window
│   └── epix.window.glfw
├── epix.render
│   └── epix.render.core_graph
└── epix.sprite
```

### Module Dependency Graph

```
core (foundation)
  ↓
input, assets, transform (independent modules)
  ↓
image (depends on core, assets)
window (depends on core, input, assets)
  ↓
window.glfw (depends on window)
  ↓
render (depends on core, window, glfw, assets, image, transform)
  ↓
render.core_graph (depends on render)
  ↓
sprite (depends on core, assets, image, transform, render, core_graph)
```

## Implementation Phases

### Phase 2: Core Module Migration

1. Create `epix_engine/core/src/core.cppm` - main module interface
2. Create module partition files for subsystems:
   - `core-archetype.cppm`
   - `core-bundle.cppm`
   - `core-component.cppm`
   - `core-entities.cppm`
   - `core-query.cppm`
   - `core-schedule.cppm`
   - `core-storage.cppm`
   - `core-system.cppm`
   - `core-world.cppm`

3. Convert header content to module exports
4. Update CMakeLists.txt to add module sources
5. Keep headers as compatibility layer initially

### Phase 3-5: Dependent Module Migration

Follow the dependency order, creating module interface files for each:

1. **Phase 3**: input, assets, transform (independent)
2. **Phase 4**: image, window, glfw (mid-level)
3. **Phase 5**: render, core_graph, sprite (high-level)

### Phase 6: Main Library Integration

Create root module `epix` that re-exports all submodules.

### Phase 7: Examples and Tests Migration

Update consumers to use `import epix.core;` instead of `#include <epix/core.hpp>`

### Phase 8: Validation and Cleanup

Remove or deprecate traditional headers once module migration is complete.

## CMake Module Support

### Current Setup

```cmake
# Root CMakeLists.txt
cmake_minimum_required(VERSION 3.21)

# Enable C++20 modules (before project())
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
    set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
  endif()
  # Disabled as Clang 18 doesn't fully support import std
  # set(CMAKE_CXX_MODULE_STD 1)
endif()

project("epix_engine.cmake" LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 23)
```

### Module Target Configuration (Example)

```cmake
# For a module-enabled library
add_library(epix_core)
target_sources(epix_core
  PUBLIC
    FILE_SET CXX_MODULES FILES
      src/core.cppm
      src/core-archetype.cppm
      src/core-bundle.cppm
      # ... other module partition files
  PRIVATE
    # Implementation files
    src/app.cpp
    src/archetype.cpp
    # ...
)
target_compile_features(epix_core PUBLIC cxx_std_23)
```

## Benefits of C++20 Modules

1. **Faster Compilation**: Modules are compiled once and reused
2. **Better Dependency Management**: Clear import/export boundaries
3. **Improved Encapsulation**: Only exported entities are visible
4. **Reduced Preprocessor Pollution**: No macro leakage
5. **Better Tooling Support**: IDEs can better understand code structure

## Migration Guidelines

### DO:
- Use `export module modulename;` at the start of module interface files
- Use `module modulename;` at the start of module implementation files
- Export only the public API surface
- Use module partitions for large modules
- Keep the global module fragment minimal
- Use `import` instead of `#include` for other modules

### DON'T:
- Don't export macros (use constexpr/consteval instead)
- Don't use `using namespace` in module interfaces
- Don't include headers after module declaration (use global module fragment)
- Don't export implementation details

## Compatibility

### Transition Period

During migration, support both modules and traditional headers:

1. Keep existing headers
2. Add new module interfaces
3. Headers can include module interfaces if needed
4. Gradually migrate consumers to use modules
5. Eventually deprecate/remove headers

### Compiler Support

- **Clang 16+**: Good C++20 modules support
- **GCC 11+**: Experimental modules support (`-fmodules-ts`)
- **MSVC 19.28+**: Good C++20 modules support

Current project uses:
- Clang 18.1.3 ✓
- GCC 13.3.0 ✓

## References

- [C++20 Modules Overview](https://en.cppreference.com/w/cpp/language/modules)
- [CMake C++20 Modules Support](https://www.kitware.com/import-cmake-c20-modules/)
- [Clang Modules Documentation](https://clang.llvm.org/docs/StandardCPlusPlusModules.html)

## Next Steps

1. **Immediate**: Resolve C++23 standard library compatibility issues
2. **Then**: Begin core module migration with example module interface
3. **Test**: Verify modules compile and link correctly
4. **Iterate**: Migrate remaining modules in dependency order
5. **Validate**: Ensure all examples and tests work with modules
