# Phase 2: Core Module Migration - Implementation Guide

## Status: Ready to Begin

With the C++23 compatibility layer in place, we can now proceed with migrating the epix_core module to use C++20 modules.

## Prerequisites

✅ Build system configured for C++20 modules  
✅ C++23 compatibility layer implemented (`epix/utils/cpp23_compat.hpp`)  
✅ Test module verified working  
⏳ Next: Apply compatibility shims to codebase and create module interfaces

## Step 1: Apply Compatibility Layer to Existing Code

Before creating module interfaces, the existing code needs to be updated to use the compatibility layer instead of directly using C++23 features.

### Files to Update

Search for and replace the following patterns:

1. **`std::move_only_function`** → `epix::compat::move_only_function` or `EPIX_MOVE_ONLY_FUNCTION`
   - Location: Throughout `epix_engine/core/include/epix/core/`
   - Files: `system_dispatcher.hpp`, `world.hpp`, etc.

2. **`std::ranges::to<>`** → `epix::compat::ranges::to<>` or `EPIX_RANGES_TO`
   - Location: Code using range conversions
   
3. **`std::views::enumerate`** → `epix::compat::views::enumerate` or `EPIX_VIEWS_ENUMERATE`
   - Location: `entities.hpp`, `archetype.hpp`, etc.

4. **`.insert_range(`** → `EPIX_INSERT_RANGE(` macro
   - Location: Vector/container operations throughout core

### Example Changes

**Before:**
```cpp
std::move_only_function<void(World&)> callback;
```

**After:**
```cpp
#include <epix/utils/cpp23_compat.hpp>
epix::compat::move_only_function<void(World&)> callback;
// or use macro:
EPIX_MOVE_ONLY_FUNCTION<void(World&)> callback;
```

**Before:**
```cpp
for (auto [idx, val] : container | std::views::enumerate) {
```

**After:**
```cpp
for (auto [idx, val] : container | epix::compat::views::enumerate) {
```

## Step 2: Verify Build with Compatibility Layer

After applying the compatibility layer:

```bash
cd /home/runner/work/epix_engine/epix_engine
git submodule update --init --recursive
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
ninja epix_core
```

Expected: Clean build with no C++23 feature errors.

## Step 3: Create Core Module Interface

Create `epix_engine/core/src/epix_core.cppm`:

```cpp
// Core module interface file
module;

// Global module fragment - traditional headers that modules can't import yet
#include <epix/utils/cpp23_compat.hpp>
#include <memory>
#include <functional>
#include <ranges>
#include <vector>
#include <string>
#include <expected>
// ... other standard library headers

export module epix_core;

// Export partitions (to be created)
export import :archetype;
export import :bundle;
export import :component;
export import :entities;
export import :query;
export import :schedule;
export import :storage;
export import :system;
export import :world;

// Export main namespace
export namespace epix::core {
    // Re-export all public API from partitions
}
```

## Step 4: Create Module Partitions

Create module partition files for each subsystem:

### Example: `epix_engine/core/src/epix_core-world.cppm`

```cpp
module;

#include <epix/utils/cpp23_compat.hpp>
#include <memory>
#include <functional>
#include <expected>

export module epix_core:world;

// Import other partitions this depends on
import :component;
import :entities;
import :storage;
import :archetype;
import :bundle;

export namespace epix::core {
    // World class definition
    struct World {
        // ... class definition from world.hpp
    };
    
    struct DeferredWorld {
        // ... class definition
    };
}
```

## Step 5: Update CMakeLists.txt

Update `epix_engine/core/CMakeLists.txt`:

```cmake
file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")

add_library(epix_core STATIC)

# Add module interface files
target_sources(epix_core
  PUBLIC
    FILE_SET CXX_MODULES FILES
      src/epix_core.cppm
      src/epix_core-archetype.cppm
      src/epix_core-bundle.cppm
      src/epix_core-component.cppm
      src/epix_core-entities.cppm
      src/epix_core-query.cppm
      src/epix_core-schedule.cppm
      src/epix_core-storage.cppm
      src/epix_core-system.cppm
      src/epix_core-world.cppm
  PRIVATE
    ${MODULE_SOURCES}
)

target_include_directories(epix_core PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(epix_core PUBLIC cxx_std_23)

# Link dependencies
target_link_libraries(epix_core PUBLIC BSThreadPool)
target_link_libraries(epix_core PUBLIC spdlog::spdlog)
if (EPIX_ENABLE_TRACY)
  target_compile_definitions(epix_core PRIVATE EPIX_ENABLE_TRACY)
  target_link_libraries(epix_core PRIVATE TracyClient)
endif()
```

## Step 6: Update Implementation Files

Update `.cpp` files to import the module instead of including headers:

**Before:**
```cpp
#include "epix/core/world.hpp"
#include "epix/core/entities.hpp"
```

**After:**
```cpp
module epix_core; // or module epix_core:world;

// Implementation...
```

## Step 7: Maintain Header Compatibility (Transition Period)

During transition, keep headers working by having them import the module:

**epix/core.hpp:**
```cpp
#pragma once

// Import the module
import epix_core;

// Re-export into global namespace for compatibility
using namespace epix::core::prelude;
```

This allows existing code to continue using `#include <epix/core.hpp>` while the migration proceeds.

## Step 8: Update Tests and Examples

Update test files to use module imports:

```cpp
import epix_core;

int main() {
    using namespace epix::core;
    World world(Main);
    // ... test code
}
```

## Step 9: Verify and Test

```bash
ninja epix_core
ninja test_core_world
ninja test_core_entities
# ... run all core tests
```

## Expected Benefits

Once complete:
- ✅ Faster compilation (modules compiled once)
- ✅ Better dependency tracking
- ✅ Cleaner separation of interface/implementation
- ✅ No macro pollution across boundaries
- ✅ Better IDE support

## Migration Order

1. Core (foundation) - this phase
2. Input, Assets, Transform (independent modules)
3. Image, Window (depend on core + independent modules)
4. GLFW (depends on window)
5. Render (depends on all above)
6. Core_graph (depends on render)
7. Sprite (depends on render + core_graph)

## Notes

- Use Clang 18.1.3 for best C++20 module support
- Keep headers during transition for backward compatibility
- Module partition files use `-` separator (e.g., `epix_core-world.cppm`)
- Module names use underscores (e.g., `epix_core`, not `epix.core`)
- Test each module independently before proceeding to next

## Ready to Proceed?

The infrastructure is in place. To begin Phase 2:

1. Apply compatibility layer to existing code
2. Verify build works
3. Create module interface files
4. Update CMakeLists.txt
5. Test and validate

All prerequisites are met! 🚀
