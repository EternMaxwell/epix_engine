# C++20 Modules in Epix Engine - Current Implementation Status

## Overview

This directory contains C++20 module interface units for the Epix Engine. The module system provides an alternative to traditional header includes while maintaining full backward compatibility.

## Implementation Strategy

### Hybrid Approach

Due to the complexity of the codebase (templates, third-party dependencies, macros), we use a **hybrid module/header approach**:

1. **Module interface units** (`.cppm` files) re-export existing header definitions
2. **Headers remain unchanged** for compatibility and template-heavy code
3. **Third-party libraries** stay as traditional headers
4. **Both `import` and `#include` work** when modules are enabled

This approach provides:
- ✅ No breaking changes to existing code
- ✅ Gradual migration path
- ✅ Module benefits where practical
- ✅ Full backward compatibility

## Current Status

### Implemented Modules

| Module | File | Status | Notes |
|--------|------|--------|-------|
| epix.transform | `transform/src/transform.cppm` | ✅ Demo | Shows module wrapping approach |
| epix.input | `input/src/input.cppm` | ✅ Demo | Input handling module |

### Planned Modules

| Module | Priority | Complexity | Dependencies |
|--------|----------|------------|--------------|
| epix.image | High | Low | epix.core, nvrhi |
| epix.assets | High | Medium | epix.core |
| epix.core | Critical | Very High | Third-party libs, templates |
| epix.window | Medium | Medium | epix.core, epix.input, glfw |
| epix.render | Low | Very High | Multiple deps, templates |
| epix.sprite | Low | Medium | epix.core, epix.render |
| epix.text | Low | High | epix.core, epix.render, freetype |

## Module Interface Structure

Each module interface file follows this pattern:

```cpp
// module_name.cppm
module;

// Global module fragment - include headers here
#include <epix/core.hpp>
#include <third_party/lib.hpp>

export module module.name;

// Re-export symbols from headers
export namespace epix::module_name {
    using ::epix::module_name::ClassName;
    using ::epix::module_name::function;
    // ...
}

// Optionally export to epix namespace
export namespace epix {
    using namespace module_name;
}
```

## Building with Modules

### Enable Modules

```bash
cmake -B build -DEPIX_ENABLE_MODULES=ON
cmake --build build
```

### Disable Modules (Default)

```bash
cmake -B build -DEPIX_ENABLE_MODULES=OFF
cmake --build build
```

## Why This Approach?

### Challenges with Full Modularization

1. **Templates**: Extensive template code (ECS queries, systems) is difficult to modularize
2. **Third-Party Code**: Cannot convert external libraries (entt, glm, nvrhi, etc.)
3. **Macros**: Macros cannot be exported from modules
4. **Build Complexity**: Full modularization would require major refactoring

### Benefits of Hybrid Approach

1. **Gradual Migration**: Can convert modules incrementally
2. **Backward Compatible**: Existing code works without changes
3. **Future-Ready**: Infrastructure in place for full migration
4. **Best of Both**: Use modules where beneficial, headers where needed

## Next Steps

### For Contributors

If you want to add a module interface unit:

1. Create `.cppm` file in module's `src/` directory
2. Follow the template pattern shown above
3. Update module's `CMakeLists.txt`:
   ```cmake
   if(EPIX_ENABLE_MODULES)
       epix_add_module_interface(target_name module.name ${CMAKE_CURRENT_SOURCE_DIR}/src/module.cppm)
   endif()
   ```
4. Test with both `EPIX_ENABLE_MODULES=ON` and `OFF`

### Priority Order

1. **Simple modules first**: image, assets, transform (done), input (done)
2. **Core module**: Critical but complex, may need partitions
3. **Dependent modules**: window, render, sprite, text
4. **Validation**: Build and test all configurations

## Technical Details

### Compiler Support

- **MSVC**: Best support, uses `.ixx` extension by default
- **Clang 16+**: Good support, uses `.cppm` extension
- **GCC 11+**: Experimental support, uses `.cppm` extension

### CMake Helper Functions

```cmake
# Configure target for module support
epix_configure_module_target(target_name)

# Add module interface unit
epix_add_module_interface(target_name module.name /path/to/module.cppm)
```

### Module Dependencies

Modules must be built in dependency order:
1. Third-party libraries (headers)
2. epix.core
3. epix.input, epix.assets, epix.transform, epix.image
4. epix.window
5. epix.render
6. epix.sprite, epix.text

## Testing

Both configurations should be tested:

```bash
# Without modules
cmake -B build-no-modules -DEPIX_ENABLE_MODULES=OFF
cmake --build build-no-modules
cd build-no-modules && ctest

# With modules
cmake -B build-with-modules -DEPIX_ENABLE_MODULES=ON
cmake --build build-with-modules
cd build-with-modules && ctest
```

## Known Limitations

1. **Not all modules implemented**: Only demo modules currently
2. **Template limitations**: Heavy template code stays in headers
3. **Macro limitations**: Macros cannot be exported
4. **Build time**: Initial module build may be slower
5. **Compiler versions**: Requires modern compiler support

## Documentation

- [MODULES.md](../../MODULES.md) - Complete modules overview
- [BUILD_WITH_MODULES.md](BUILD_WITH_MODULES.md) - Build instructions
- [MIGRATION_REPORT.md](MIGRATION_REPORT.md) - Migration status
- [EXAMPLES.md](EXAMPLES.md) - Usage examples

## Questions?

See the documentation above or the main MODULES.md file in the repository root.
