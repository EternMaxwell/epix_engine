cmake_minimum_required(VERSION 3.20)

# Standalone test for WebGPU wrapper (header-only mode)
# This can be built independently to verify the infrastructure

project(webgpu_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add test executable
add_executable(test_webgpu_header test_header_compile.cpp)

# Include generated WebGPU headers
target_include_directories(test_webgpu_header PRIVATE
    ${CMAKE_BINARY_DIR}/generated/webgpu
)

# Link against wgpu_native
if (TARGET wgpu_native)
    target_link_libraries(test_webgpu_header PRIVATE wgpu_native)
    
    # Copy DLL if needed
    if (COMMAND target_copy_wgpu_binaries)
        target_copy_wgpu_binaries(test_webgpu_header)
    endif()
else()
    message(WARNING "wgpu_native target not found. Test will compile but may not link.")
endif()

# Add as CTest
enable_testing()
add_test(NAME test_webgpu_header COMMAND test_webgpu_header)
