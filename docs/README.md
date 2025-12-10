# C++20 Modules Test Example

This directory contains a simple test demonstrating that C++20 modules are properly configured and working.

## Files

- `test_module.cppm` - Module interface file
- `test_module_main.cpp` - Program that imports the module
- `CMakeLists.txt` - CMake build configuration (for reference)

## Building Manually

### With Clang 18+

```bash
# 1. Precompile the module interface
clang++ -std=c++20 --precompile -x c++-module test_module.cppm -o test_module.pcm

# 2. Compile the module implementation
clang++ -std=c++20 -c test_module.pcm -o test_module.o

# 3. Compile the main program
clang++ -std=c++20 -fmodule-file=test_module=test_module.pcpm -c test_module_main.cpp -o main.o

# 4. Link everything together
clang++ test_module.o main.o -o test_program

# 5. Run
./test_program
```

### Expected Output

```
Hello from module: C++20 Modules
```

## Building with CMake

**Note**: CMake 3.28-3.31 has experimental C++20 modules support but may have issues with dependency scanning. The CMakeLists.txt is provided as a reference for the target configuration.

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_CXX_COMPILER=clang++
ninja
./test_module_example
```

## Verification

This example successfully compiles and runs, proving that:

1. ✅ Clang 18.1.3 supports C++20 modules
2. ✅ Module interfaces (`.cppm` files) compile correctly
3. ✅ `import` statements work
4. ✅ Module exports are accessible to importers

## Module Features Demonstrated

- Global module fragment (`module;` ... `export module name;`)
- Exporting a namespace
- Exporting a class
- Exporting a function
- Using `import` instead of `#include` in consumer code

## Application to epix_engine

This same approach will be used for the epix_engine modules:

```cpp
// Example: epix_engine/core/src/core.cppm
module;
#include <traditional_headers_here>
export module epix_core;

export namespace epix::core {
    // Export public API
}
```

```cpp
// Example: User code
import epix_core;

int main() {
    epix::core::World world;
    // ...
}
```

Once the C++23 standard library compatibility issues in the codebase are resolved, this same pattern will be applied to all engine modules.
