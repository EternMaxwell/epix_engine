# C++20 Modules Examples

## Example 1: Simple Application with Modules

### main.cpp (with modules enabled)

```cpp
// When EPIX_ENABLE_MODULES=ON, you can use module imports
import epix.core;

// Or use traditional includes (both work)
// #include <epix/core.hpp>

int main() {
    using namespace epix;
    
    // Create application
    App app;
    
    // Add systems
    app.add_systems(Update, []() {
        spdlog::info("Hello from module-enabled Epix Engine!");
    });
    
    // Run application
    app.run();
    
    return 0;
}
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyEpixApp)

# Enable C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Epix Engine
add_subdirectory(epix_engine)

# Option to use modules
option(USE_EPIX_MODULES "Build with C++20 modules" OFF)

if(USE_EPIX_MODULES)
    set(EPIX_ENABLE_MODULES ON CACHE BOOL "Enable Epix modules" FORCE)
endif()

# Create executable
add_executable(my_app main.cpp)

# Link Epix libraries
target_link_libraries(my_app PRIVATE 
    epix_core
)

# Configure for modules if enabled
if(EPIX_ENABLE_MODULES)
    epix_configure_module_target(my_app)
endif()
```

## Example 2: Rendering Application

### renderer_app.cpp

```cpp
// Module imports (when EPIX_ENABLE_MODULES=ON)
import epix.core;
import epix.render;
import epix.window;

// Or traditional includes
// #include <epix/core.hpp>
// #include <epix/render.hpp>
// #include <epix/window.hpp>

int main() {
    using namespace epix;
    
    App app;
    
    // Add plugins
    app.add_plugin(RenderPlugin{});
    app.add_plugin(WindowPlugin{});
    
    // Run
    app.run();
    
    return 0;
}
```

### CMakeLists.txt

```cmake
add_executable(renderer_app renderer_app.cpp)

target_link_libraries(renderer_app PRIVATE 
    epix_core
    epix_render
    epix_window
)

if(EPIX_ENABLE_MODULES)
    epix_configure_module_target(renderer_app)
endif()
```

## Example 3: Mixed Module and Header Usage

Sometimes you may want to use modules for engine code but headers for your own code:

```cpp
// Use modules for Epix Engine
import epix.core;
import epix.render;

// Use headers for your own code or third-party libraries
#include "my_custom_component.hpp"
#include <glm/glm.hpp>

struct MySystem {
    void operator()(Query<Item<Entity, const Transform&>> query) {
        for (auto [entity, transform] : query.iter()) {
            // Use both module and header code
            spdlog::info("Entity {} at position {}, {}, {}",
                entity,
                transform.translation.x,
                transform.translation.y,
                transform.translation.z);
        }
    }
};

int main() {
    App app;
    app.add_systems(Update, MySystem{});
    app.run();
}
```

## Example 4: Gradual Migration

You can gradually migrate from headers to modules:

### Step 1: Start with headers (current code)

```cpp
#include <epix/core.hpp>
#include <epix/render.hpp>

int main() {
    epix::App app;
    // ...
}
```

### Step 2: Enable modules in build

```bash
cmake -B build -DEPIX_ENABLE_MODULES=ON
```

### Step 3: Optionally switch to imports

```cpp
import epix.core;
import epix.render;

int main() {
    epix::App app;
    // ...
}
```

**Note**: Both `import` and `#include` work when modules are enabled!

## Build Instructions

### Building with Headers (Default)

```bash
cmake -B build
cmake --build build
./build/my_app
```

### Building with Modules

```bash
# Using CMake option
cmake -B build -DEPIX_ENABLE_MODULES=ON
cmake --build build
./build/my_app

# Using preset
cmake --preset=debug-msvc-modules
cmake --build --preset=debug-msvc-modules
```

## Module Import Reference

### Available Modules

```cpp
import epix.core;        // Core ECS functionality
import epix.input;       // Input handling  
import epix.assets;      // Asset loading
import epix.window;      // Window management
import epix.transform;   // Transform components
import epix.image;       // Image handling
import epix.render;      // Rendering system
import epix.sprite;      // 2D sprites
import epix.text;        // Text rendering
```

### Equivalent Headers

```cpp
#include <epix/core.hpp>
#include <epix/input.hpp>
#include <epix/assets.hpp>
#include <epix/window.hpp>
#include <epix/transform.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/text.hpp>
```

## Best Practices

### 1. Use Module Imports for Epix Code

```cpp
import epix.core;
import epix.render;
```

### 2. Use Headers for Third-Party Libraries

```cpp
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
```

### 3. Maintain Both in Your Project

Support both module and header builds:

```cmake
if(EPIX_ENABLE_MODULES)
    epix_configure_module_target(my_target)
endif()
```

### 4. Test Both Configurations

```bash
# Test without modules
cmake -B build-headers -DEPIX_ENABLE_MODULES=OFF
cmake --build build-headers
ctest --test-dir build-headers

# Test with modules
cmake -B build-modules -DEPIX_ENABLE_MODULES=ON
cmake --build build-modules
ctest --test-dir build-modules
```

## Troubleshooting

### Import Not Found

If you get "module not found" errors:
1. Ensure `EPIX_ENABLE_MODULES=ON` is set
2. Verify compiler supports C++20 modules
3. Check compiler version meets requirements
4. Fall back to `#include` if needed

### Link Errors

If you get linker errors:
1. Ensure you're linking the same libraries
2. Check that both module and source files are built
3. Verify no duplicate symbols

### Template Errors

Some template code may need headers:
```cpp
import epix.core;
#include <epix/core/query/query.hpp>  // For complex templates
```

## Performance Notes

- **Build Time**: First module build may be slower
- **Incremental Builds**: Often faster with modules
- **Runtime**: No performance difference
- **Binary Size**: Similar size to header builds

## Compiler-Specific Notes

### MSVC
- Best module support
- Use `.ixx` extension for your own modules
- Requires Visual Studio 2022 or later

### Clang
- Good module support since Clang 16
- Use `.cppm` extension
- May need `-fmodules` flag

### GCC
- Experimental module support
- Use `.cppm` extension  
- May have limitations with complex templates

---

See [BUILD_WITH_MODULES.md](BUILD_WITH_MODULES.md) for complete build instructions.
