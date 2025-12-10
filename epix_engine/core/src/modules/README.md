# C++20 Modules Migration - Current Status

## Overview

This directory contains the C++20 module interface files for the epix_engine codebase. The migration is in progress, converting from traditional header-based compilation to modern C++20 modules with C++23 feature support.

## What Has Been Completed

### Infrastructure (✓ Complete)
- ✅ CMake configuration for C++20 modules support (root CMakeLists.txt)
- ✅ Compiler-specific flags for GCC 13+, Clang 18+, and MSVC
- ✅ Feature detection macros (`epix_engine/include/epix/module_support.hpp`)
- ✅ C++23 conditional compilation support

### Proof-of-Concept Modules (✓ Complete)
- ✅ `epix.core:fwd` - Forward declarations partition (fwd.cppm)
- ✅ `epix.core:tick` - Tick system with C++23 deducing this support (tick.cppm)

### Documentation (✓ Complete)
- ✅ Comprehensive migration guide (`documentation/CPP20_MODULES_MIGRATION.md`)
- ✅ Module architecture documentation
- ✅ Conversion patterns and examples
- ✅ Migration order and dependency graph

## Module Structure

The epix_engine modules follow this hierarchy:

```
epix_engine/core/src/modules/
├── core.cppm           # Primary module interface (to be created)
├── fwd.cppm           # Forward declarations partition ✓
├── tick.cppm          # Tick system partition ✓
├── meta.cppm          # Type metadata partition (to be created)
├── type_system.cppm   # Type registry partition (to be created)
├── entities.cppm      # Entity management partition (to be created)
├── component.cppm     # Component system partition (to be created)
├── storage.cppm       # Storage system partition (to be created)
├── archetype.cppm     # Archetype system partition (to be created)
├── bundle.cppm        # Bundle system partition (to be created)
├── world.cppm         # World partition (to be created)
├── query.cppm         # Query system partition (to be created)
├── system.cppm        # System execution partition (to be created)
├── schedule.cppm      # Scheduling partition (to be created)
├── event.cppm         # Event system partition (to be created)
├── app.cppm           # Application framework partition (to be created)
├── hierarchy.cppm     # Hierarchy partition (to be created)
└── change_detection.cppm  # Change detection partition (to be created)
```

## How to Continue the Migration

### Step 1: Complete Core Module Partitions

Convert remaining headers to module partitions in dependency order:

1. `meta.cppm` - Type metadata (depends on: fwd)
2. `type_system.cppm` - Type registry (depends on: fwd, meta)
3. `entities.cppm` - Entity management (depends on: fwd, tick)
4. `component.cppm` - Component system (depends on: fwd, meta, type_system)
5. Continue with remaining partitions...

### Step 2: Create Primary Module Interface

Create `core.cppm` that imports and re-exports all partitions:

```cpp
module;

// Global module fragment with third-party includes
#include <BS/thread_pool.hpp>
#include <spdlog/spdlog.h>
// ... other third-party headers

export module epix.core;

// Re-export all partitions
export import :fwd;
export import :tick;
export import :meta;
// ... other partitions

// Export prelude namespace
export namespace epix::core::prelude {
    // using declarations
}
```

### Step 3: Update CMakeLists.txt

Add module source files to the build:

```cmake
# epix_engine/core/CMakeLists.txt

file(GLOB_RECURSE MODULE_INTERFACES "src/modules/*.cppm")
file(GLOB_RECURSE MODULE_IMPLS "src/modules/*.cpp")
file(GLOB_RECURSE LEGACY_SOURCES "src/*.cpp")

add_library(epix_core)

# Add module interface files
target_sources(epix_core
    PUBLIC
        FILE_SET CXX_MODULES FILES ${MODULE_INTERFACES}
    PRIVATE
        ${MODULE_IMPLS}
        ${LEGACY_SOURCES}  # Keep during transition
)

target_link_libraries(epix_core PUBLIC 
    BSThreadPool 
    spdlog::spdlog
    EnTT::EnTT
)

# Enable module scanning
set_target_properties(epix_core PROPERTIES
    CXX_SCAN_FOR_MODULES ON
)
```

### Step 4: Convert Each Remaining Module

Follow the same pattern for each module:
1. input → assets → window → transform → image → render → sprite
2. Each module depends on previously completed modules
3. Use import statements for epix modules, global fragment for third-party

### Step 5: Update Tests and Examples

Convert from `#include` to `import`:

```cpp
// Before
#include <epix/core.hpp>

// After
import epix.core;
```

## Feature Detection Usage

The codebase uses conditional compilation for C++23 features:

```cpp
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
    // C++23 deducing this version
    auto method(this auto&& self) { /* ... */ }
#else
    // C++20 fallback version
    auto method() & { /* ... */ }
    auto method() const& { /* ... */ }
#endif
```

This ensures compatibility with:
- GCC 13+ (partial C++23 support)
- Clang 18+ (better C++23 support)
- MSVC 19.30+ (good C++23 support)

## Testing the Module Build

To test the current module implementation:

```bash
cd <epix_engine_root>
mkdir build && cd build

# Configure with modules enabled
cmake .. -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Debug

# Build (this will fail until core.cppm is created and CMakeLists updated)
cmake --build .
```

## Remaining Work

### High Priority
- [ ] Create remaining core module partitions (13 partitions)
- [ ] Create primary core.cppm interface
- [ ] Update core/CMakeLists.txt for module build
- [ ] Test core module build

### Medium Priority
- [ ] Convert input module
- [ ] Convert assets module
- [ ] Convert window module
- [ ] Convert transform module
- [ ] Convert image module

### Lower Priority
- [ ] Convert render module (largest module, ~40 headers)
- [ ] Convert sprite module
- [ ] Create primary epix module
- [ ] Convert all tests
- [ ] Convert all examples

### Validation
- [ ] Build with GCC 13
- [ ] Build with Clang 18
- [ ] Build with MSVC (if available)
- [ ] All tests pass
- [ ] Performance comparable to header build

## Estimated Effort

Based on the proof-of-concept work:

- **Core module**: ~20 hours (13 remaining partitions + integration)
- **Other modules**: ~20 hours (7 modules)
- **Tests & Examples**: ~8 hours
- **Validation & Fixes**: ~10 hours
- **Total**: ~58 hours

## Notes for Future Developers

1. **Partition Dependencies**: Always import required partitions before defining types
2. **Third-Party Headers**: Keep all third-party includes in global module fragment
3. **Template Definitions**: Must be in module interface, not implementation units
4. **C++23 Features**: Always provide C++20 fallback using feature detection
5. **Build Times**: Initial module build is slow; subsequent builds are fast
6. **Module Cache**: Don't commit .gcm, .pcm, or .ifc files to git

## References

- [C++20 Modules Documentation](https://en.cppreference.com/w/cpp/language/modules)
- [CMake Modules Support](https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html)
- [GCC Modules](https://gcc.gnu.org/wiki/cxx-modules)
- [Clang Modules](https://clang.llvm.org/docs/StandardCPlusPlusModules.html)
- Migration Guide: `documentation/CPP20_MODULES_MIGRATION.md`
