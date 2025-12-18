# Building Epix Engine with C++20 Modules

## Quick Start

### Enable Modules

Use the CMake option `EPIX_ENABLE_MODULES=ON`:

```bash
# Configure with modules enabled
cmake -B build -DEPIX_ENABLE_MODULES=ON

# Build
cmake --build build

# Test
cd build && ctest
```

### Using Presets

CMake presets are available for module builds:

```bash
# MSVC with modules
cmake --preset=debug-msvc-modules

# Clang with modules
cmake --preset=debug-clang-modules

# GCC with modules  
cmake --preset=debug-gcc-modules
```

## Compiler Support

### Minimum Versions

| Compiler | Version | Flags | Status |
|----------|---------|-------|--------|
| MSVC | 19.29+ (VS 2019 16.10+) | `/std:c++20 /interface` | Supported |
| Clang | 16.0+ | `-std=c++20 -fmodules` | Supported |
| GCC | 11.0+ | `-std=c++20 -fmodules-ts` | Experimental |

## Build Configurations

### Without Modules (Default)

```bash
cmake -B build
cmake --build build
```

This is the standard header-based build with full backward compatibility.

### With Modules

```bash
cmake -B build -DEPIX_ENABLE_MODULES=ON
cmake --build build
```

This enables C++20 module interface units while maintaining header compatibility.

## Testing

Run tests with both configurations:

```bash
# Test without modules
cmake -B build-headers -DEPIX_ENABLE_MODULES=OFF
cmake --build build-headers
cd build-headers && ctest

# Test with modules
cmake -B build-modules -DEPIX_ENABLE_MODULES=ON
cmake --build build-modules
cd build-modules && ctest
```

## Further Reading

See [MODULES.md](../../MODULES.md) for complete documentation.
