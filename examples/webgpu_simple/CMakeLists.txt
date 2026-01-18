cmake_minimum_required(VERSION 3.30)

# Simple WebGPU example
project(webgpu_simple CXX)

# Require C++23 for module support
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add executable
add_executable(webgpu_simple main.cpp)

# Link against WebGPU wrapper (which provides the module)
target_link_libraries(webgpu_simple PRIVATE webgpu_wrapper)

# Copy WebGPU runtime DLLs if using shared linking
if (TARGET wgpu_native AND COMMAND target_copy_wgpu_binaries)
    target_copy_wgpu_binaries(webgpu_simple)
endif()

# Installation
install(TARGETS webgpu_simple DESTINATION bin)
