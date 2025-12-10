# C++20 Modules Refactoring Guide for Epix Engine

## Overview
This document describes the strategy and implementation pattern for converting the Epix Engine codebase from traditional header files to C++20 modules.

## Compiler Support
- **GCC 13+**: Partial C++20 modules support via `-fmodules-ts`
- **Clang 18+**: Better C++20 modules support via `-fmodules`  
- **MSVC**: C++20 modules support via `/experimental:module`

Note: Standard library modules (`import std;`) have varying support. We use traditional includes within module purview as an interim solution.

## Architecture

### Module Structure
The engine is organized into the following primary modules:

```
epix.core           - Core ECS functionality (entities, components, systems, world)
├─ :api             - API macros and basic types
├─ :meta            - Type metadata system
├─ :type_system     - Type registry
├─ :tick            - Change detection ticks
├─ :storage         - Component storage
├─ :archetype       - Archetype management
├─ :entities        - Entity management
├─ :component       - Component definitions
├─ :bundle          - Component bundles
├─ :query           - Entity queries
├─ :system          - System execution
├─ :event           - Event system
├─ :schedule        - System scheduling
├─ :world           - World abstraction
├─ :app             - Application framework
└─ :hierarchy       - Entity hierarchies

epix.assets         - Asset loading and management
epix.window         - Window management
epix.input          - Input handling
epix.transform      - Transform components
epix.image          - Image loading and processing
epix.render         - Rendering system
epix.sprite         - Sprite rendering
```

### Module Partitions
Each major module is split into partitions (sub-modules) for:
1. Faster compilation (only changed partitions need recompilation)
2. Better organization
3. Reduced coupling
4. Clearer dependency management

## Conversion Pattern

### Step 1: Create Module Directory
For each module, create a `modules/` directory:
```
epix_engine/<module>/
├─ include/          # Legacy headers (kept during transition)
├─ src/              # Legacy source files
├─ modules/          # New module interface files
│  ├─ epix.<module>.cppm           # Main module interface
│  ├─ epix.<module>-<partition>.cppm  # Module partitions
│  └─ *.cpp          # Module implementation units
├─ CMakeLists.txt    # Build configuration
└─ tests/
```

### Step 2: Create Module Interface (.cppm)
Module interface files define the public API:

```cpp
// File: modules/epix.image.cppm
export module epix.image;

// Include non-module dependencies
#include <nvrhi/nvrhi.h>
#include <stb_image.h>

// Import module dependencies (when available)
import epix.core;
import epix.assets;

// Export public API
export namespace epix::image {
    class Image { /* ... */ };
    struct ImagePlugin { /* ... */ };
}
```

### Step 3: Create Module Implementation (.cpp)
Implementation files import the module:

```cpp
// File: modules/image.cpp
module epix.image;

// Implementation of exported entities
Image Image::srgba8norm(uint32_t width, uint32_t height) {
    // ...
}
```

### Step 4: Update CMakeLists.txt
Add module support with fallback to legacy headers:

```cmake
option(EPIX_USE_MODULES "Use C++20 modules" ON)

if(EPIX_USE_MODULES)
    file(GLOB_RECURSE MODULE_SOURCES "modules/*.cppm" "modules/*.cpp")
    add_library(epix_image STATIC ${MODULE_SOURCES})
    target_compile_features(epix_image PUBLIC cxx_std_23)
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(epix_image PUBLIC -fmodules-ts)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(epix_image PUBLIC -fmodules)
    endif()
else()
    # Legacy build
    file(GLOB_RECURSE MODULE_SOURCES "src/*.cpp")
    add_library(epix_image STATIC ${MODULE_SOURCES})
    target_include_directories(epix_image PUBLIC include/)
endif()
```

### Step 5: Handle Third-Party Dependencies
Third-party libraries without module support need traditional includes:

```cpp
export module epix.image;

// Non-module headers included in module purview
#include <nvrhi/nvrhi.h>
#include <stb_image.h>

// When third-party libs get module support, replace with:
// import nvrhi;
// import stb;
```

## Feature Compatibility

### C++23 Features with GCC/Clang Support Matrix

| Feature | Header | GCC 13 | Clang 18 | Fallback |
|---------|--------|--------|----------|----------|
| `std::expected` | `<expected>` | ✅ | ✅ | Required |
| `std::move_only_function` | `<functional>` | ✅ | ✅ | `std::function` |
| `std::format` | `<format>` | ✅ | ✅ | `spdlog::fmt` |
| `std::mdspan` | `<mdspan>` | ❌ | Partial | Optional |
| Concepts | `<concepts>` | ✅ | ✅ | Required |
| Ranges | `<ranges>` | ✅ | ✅ | Required |

Use `epix/api/features.hpp` for feature detection:
```cpp
#include <epix/api/features.hpp>

#if EPIX_HAS_MDSPAN
    import std.mdspan;
#else
    // Use alternative implementation
#endif
```

## Migration Order

### Phase 1: Foundation (Current)
1. ✅ Create feature compatibility header
2. ✅ Convert smallest module (image) as proof-of-concept
3. ⏳ Update root CMakeLists.txt for module support

### Phase 2: Core Infrastructure
1. Convert `epix.core` module (largest, most complex)
   - Start with `epix.core:api` partition
   - Convert `epix.core:meta` partition
   - Continue with remaining partitions
2. Convert `epix.assets` module
3. Convert `epix.window` module
4. Convert `epix.input` module

### Phase 3: Graphics Stack
1. Convert `epix.transform` module
2. Convert `epix.render` module
3. Convert `epix.sprite` module

### Phase 4: Integration
1. Update all examples to use modules
2. Update all tests to use modules
3. Remove legacy header files
4. Update documentation

## Testing Strategy

### During Conversion
1. Keep both module and header versions
2. Build both versions in CI
3. Run tests against both versions
4. Compare performance

### Module Validation
```cmake
# Test that module compiles
add_executable(test_module_image test.cpp)
target_link_libraries(test_module_image epix_image)

# Test imports work correctly
# In test.cpp:
import epix.image;
int main() {
    auto img = epix::image::Image::srgba8norm(100, 100);
    return 0;
}
```

## Performance Considerations

### Build Time
- **Modules**: Faster incremental builds (only changed partitions recompile)
- **Headers**: Slower incremental builds (changes cascade through includes)

### Runtime
- **No runtime difference**: Modules are a build-time feature
- **Better optimization**: Compilers can optimize across module boundaries

### Binary Size
- **Potentially smaller**: Better duplicate template elimination
- **More consistent**: Less template bloat from header re-instantiation

## Common Issues and Solutions

### Issue: Circular Dependencies
**Problem**: Module A imports module B which imports module A

**Solution**: Use module partitions or forward declarations
```cpp
// epix.core:fwd.cppm - forward declarations partition
export module epix.core:fwd;
export namespace epix::core {
    class World;
    struct Entity;
}

// epix.core:world.cppm - imports fwd partition
export module epix.core:world;
import :fwd;
export namespace epix::core {
    class World { /* full definition */ };
}
```

### Issue: Template Instantiation Across Modules
**Problem**: Templates defined in one module, instantiated in another

**Solution**: Either:
1. Define templates in module interface (header-only style)
2. Explicitly instantiate common cases in module implementation

```cpp
// Option 1: Define in interface
export module epix.core;
export template<typename T>
class Component {
    // Full definition in interface
};

// Option 2: Explicit instantiation
module epix.core;
template class Component<int>;
template class Component<float>;
```

### Issue: Macro Dependencies
**Problem**: Macros don't work across module boundaries

**Solution**: 
1. Convert macros to constexpr functions/variables
2. Use module partitions for macro-dependent code
3. Include macro headers before module declaration

```cpp
// Include macro headers in global module fragment
module;
#include "macros.h"  // Macros available in module
export module epix.core;

// Or convert to constexpr
export constexpr auto EPIX_VERSION = "1.0.0";
```

## Benefits of This Refactoring

1. **Faster Builds**: Modules compile once, not per translation unit
2. **Better Encapsulation**: Only exported entities are visible
3. **Clearer Dependencies**: Explicit import statements show dependencies
4. **Reduced Coupling**: Module boundaries enforce clean architecture
5. **Future-Proof**: Aligns with modern C++ direction
6. **Better Tooling**: IDEs and static analyzers work better with modules

## References

- [C++20 Modules - cppreference](https://en.cppreference.com/w/cpp/language/modules)
- [GCC C++ Modules](https://gcc.gnu.org/wiki/cxx-modules)
- [Clang Modules](https://clang.llvm.org/docs/Modules.html)
- [CMake Modules Support](https://www.kitware.com/import-cmake-c20-modules/)
