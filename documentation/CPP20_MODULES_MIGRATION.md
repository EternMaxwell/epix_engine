# C++20 Modules Migration Guide for epix_engine

## Overview

This guide documents the comprehensive migration of epix_engine from traditional header-based compilation to C++20 modules. The migration maintains C++23 feature compatibility while ensuring GCC 13+ and Clang 18+ support.

## Architecture

### Module Organization

The epix_engine uses a hierarchical module structure:

```
epix (primary module)
├── epix.core (ECS foundation)
│   ├── :fwd (forward declarations)
│   ├── :meta (type metadata)
│   ├── :type_system (type registry)
│   ├── :entities (entity management)
│   ├── :component (component system)
│   ├── :storage (data storage)
│   ├── :archetype (archetype system)
│   ├── :bundle (component bundles)
│   ├── :world (ECS world)
│   ├── :query (query system)
│   ├── :system (system execution)
│   ├── :schedule (scheduling)
│   ├── :event (event system)
│   ├── :app (application framework)
│   ├── :hierarchy (entity hierarchy)
│   └── :change_detection (change tracking)
├── epix.input (input handling)
├── epix.assets (asset management)
├── epix.window (window management)
│   └── epix.window.glfw (GLFW backend)
├── epix.transform (transform components)
├── epix.image (image handling)
├── epix.render (rendering system)
│   ├── :vulkan (Vulkan integration)
│   ├── :graph (render graph)
│   └── :pipeline (render pipelines)
└── epix.sprite (sprite rendering)
```

## Module File Structure

### Primary Module Interface (.cppm)

Location: `epix_engine/{module}/src/modules/{module}.cppm`

```cpp
// Example: epix_engine/core/src/modules/core.cppm

module;

// Global module fragment - third-party headers
#include <third_party_header.h>
#include <standard_library_header>

export module epix.core;

// Import and re-export partitions
export import :fwd;
export import :meta;
// ... other partitions

// Export primary interfaces
export namespace epix::core {
    // Main types and functions
}
```

### Module Partition Interface

Location: `epix_engine/{module}/src/modules/{partition}.cppm`

```cpp
// Example: epix_engine/core/src/modules/fwd.cppm

module;

// Minimal includes needed for partition

export module epix.core:fwd;

export namespace epix::core {
    struct Entity;
    struct World;
    // ... forward declarations
}
```

### Module Implementation Unit

Location: `epix_engine/{module}/src/modules/{name}.cpp`

```cpp
// Example: epix_engine/core/src/modules/world_impl.cpp

module;

#include <required_headers.h>

module epix.core;

// Implementations of exported functions
namespace epix::core {
    void World::some_method() {
        // implementation
    }
}
```

## CMake Integration

### Module Compilation Setup

```cmake
# Enable C++23 and modules
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
    set(CMAKE_CXX_MODULE_STD 1)
endif()

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-fmodules-ts)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fmodules -fbuiltin-module-map)
elseif(MSVC)
    add_compile_options(/experimental:module /ifcOutput ${CMAKE_BINARY_DIR}/ifc/)
endif()
```

### Module Library Target

```cmake
# Example for epix_core module
file(GLOB_RECURSE MODULE_INTERFACES "src/modules/*.cppm")
file(GLOB_RECURSE MODULE_IMPLS "src/modules/*.cpp")

add_library(epix_core)
target_sources(epix_core
    PUBLIC
        FILE_SET CXX_MODULES FILES ${MODULE_INTERFACES}
    PRIVATE
        ${MODULE_IMPLS}
)

target_link_libraries(epix_core PUBLIC BSThreadPool spdlog::spdlog EnTT::EnTT)
```

## Migration Patterns

### Pattern 1: Simple Type Conversion

**Before (header):**
```cpp
// epix_engine/transform/include/epix/transform.hpp
#pragma once
#include <glm/glm.hpp>

namespace epix::transform {
    struct Transform {
        glm::vec3 position;
        glm::quat rotation;
    };
}
```

**After (module):**
```cpp
// epix_engine/transform/src/modules/transform.cppm
module;
#include <glm/glm.hpp>

export module epix.transform;

export namespace epix::transform {
    struct Transform {
        glm::vec3 position;
        glm::quat rotation;
    };
}
```

### Pattern 2: Template Type with C++23 Features

**Before:**
```cpp
#pragma once
namespace epix {
    template<typename T>
    struct Wrapper {
        auto get(this auto&& self) { return std::forward<decltype(self)>(self).value; }
        T value;
    };
}
```

**After:**
```cpp
module;
#include <utility>

export module epix.wrapper;

export namespace epix {
    template<typename T>
    struct Wrapper {
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
        auto get(this auto&& self) { 
            return std::forward<decltype(self)>(self).value; 
        }
#else
        T& get() { return value; }
        const T& get() const { return value; }
#endif
        T value;
    };
}
```

### Pattern 3: Partition with Internal Dependencies

**Partition Interface:**
```cpp
// epix_engine/core/src/modules/query.cppm
module;
#include <concepts>
#include <tuple>

export module epix.core:query;

import :fwd;
import :entities;
import :component;

export namespace epix::core::query {
    template<typename... Components>
    struct Query {
        // implementation
    };
}
```

### Pattern 4: Third-Party Library Integration

When third-party libraries don't support modules:

```cpp
module;

// Put all third-party includes in global module fragment
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>

export module epix.core;

// Use types from third-party libraries normally
export namespace epix::core {
    void log_message(const std::string& msg) {
        spdlog::info(msg);
    }
}
```

## Migration Order

Modules must be migrated in dependency order:

1. **epix.core** (foundation, no internal dependencies)
   - Start with :fwd partition
   - Then :meta, :type_system
   - Then dependent partitions
   
2. **epix.input** (depends on core)

3. **epix.assets** (depends on core)

4. **epix.window** (depends on core, input, assets)
   - epix.window.glfw sub-module

5. **epix.transform** (depends on core)

6. **epix.image** (depends on core, assets)

7. **epix.render** (depends on core, window, assets, image, transform)
   - Largest module, convert in phases

8. **epix.sprite** (depends on core, render)

9. **epix** (primary module, re-exports all)

## Testing Migration

### Test File Conversion

**Before:**
```cpp
// test_world.cpp
#include <epix/core.hpp>

int main() {
    epix::World world;
    // ...
}
```

**After:**
```cpp
// test_world.cpp
import epix.core;

int main() {
    epix::World world;
    // ...
}
```

### CMake Test Configuration

```cmake
add_executable(test_core_world test_world.cpp)
target_link_libraries(test_core_world PRIVATE epix_core)
```

## Feature Detection

Use the provided `module_support.hpp` for conditional compilation:

```cpp
#include "epix/module_support.hpp"

#if EPIX_HAS_STD_EXPECTED
    // Use std::expected
#else
    // Use alternative
#endif

// Or use constexpr:
if constexpr (epix::module::has_cpp23) {
    // C++23-specific code
}
```

## Common Issues and Solutions

### Issue 1: Circular Dependencies

**Problem:** Module A needs B, B needs A

**Solution:** Use forward declarations in a shared :fwd partition

```cpp
export module epix.core:fwd;
export namespace epix::core {
    struct World;
    struct Entity;
}
```

### Issue 2: Template Instantiation

**Problem:** Templates not instantiating properly

**Solution:** Export template definitions in module interface

```cpp
export module epix.core:query;

export namespace epix::core {
    template<typename T>
    struct Query {
        void execute() { /* impl */ }
    };
}
```

### Issue 3: Macro Dependencies

**Problem:** Macros from headers not available

**Solution:** Move macros to global module fragment or use constexpr

```cpp
module;

#define EPIX_MACRO(x) x * 2  // In global fragment if needed

export module epix.core;

// Or prefer constexpr:
export constexpr int epix_function(int x) { return x * 2; }
```

## Build Time Optimization

Module compilation can be slow initially. Optimize with:

1. **Parallel Module Compilation:**
```cmake
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
```

2. **Module Caching:**
   - GCC: Uses `.gcm` files
   - Clang: Uses `.pcm` files
   - MSVC: Uses `.ifc` files

3. **Minimize Module Interface Size:**
   - Export only public API
   - Use partitions to isolate internal dependencies

## Validation Checklist

- [ ] All module interfaces compile without errors
- [ ] All tests pass with modules
- [ ] Build succeeds on GCC 13+
- [ ] Build succeeds on Clang 18+
- [ ] Build succeeds on MSVC 19.30+
- [ ] C++23 features work with feature detection
- [ ] No circular dependencies between modules
- [ ] Module BMI files generated correctly
- [ ] Link time comparable to header-based build

## Current Status

See the main project README and PR description for current migration progress.
