# WebGPU integration for epix_engine
# This file sets up WebGPU with optional C++20 module support

# Options
option(EPX_WGPU_USE_MODULE "Generate and use C++20 module for WebGPU" ON)
option(EPX_WGPU_GENERATE_ON_CONFIGURE "Generate WebGPU wrapper during CMake configure (vs build time)" ON)
option(EPX_USE_WEBGPU_DISTRIBUTION "Use webgpu-distribution patterns for fetching" ON)

set(EPX_WGPU_NATIVE_VERSION "v24.0.3.1" CACHE STRING "Version of wgpu-native to fetch")
set(EPX_WGPU_LINK_TYPE "SHARED" CACHE STRING "Link type for wgpu-native (SHARED or STATIC)")
set(EPX_WGPU_MODULE_NAME "webgpu" CACHE STRING "Name of the generated C++20 module")

# Include utility functions
include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)

# Fetch wgpu-native binaries
include(${CMAKE_CURRENT_LIST_DIR}/FetchWgpuNative.cmake)

# Fetch WebGPU-Cpp generator
include(${CMAKE_CURRENT_LIST_DIR}/WebGPUCppGenerator.cmake)

# Set output directory for generated files
set(WEBGPU_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/webgpu")

# Generate WebGPU wrapper
if (EPX_WGPU_GENERATE_ON_CONFIGURE AND WEBGPU_CPP_GENERATOR_AVAILABLE)
    # Get header files from wgpu-native
    set(WEBGPU_HEADERS
        "${WGPU_NATIVE_DIR}/include/webgpu/webgpu.h"
    )
    
    # Check if wgpu.h exists (backend-specific extensions)
    if (EXISTS "${WGPU_NATIVE_DIR}/include/webgpu/wgpu.h")
        list(APPEND WEBGPU_HEADERS "${WGPU_NATIVE_DIR}/include/webgpu/wgpu.h")
    endif()

    # Generate wrapper
    generate_webgpu_wrapper(
        OUTPUT_DIR ${WEBGPU_GENERATED_DIR}
        HEADER_FILES ${WEBGPU_HEADERS}
        GENERATE_MODULE ${EPX_WGPU_USE_MODULE}
        MODULE_NAME ${EPX_WGPU_MODULE_NAME}
    )
endif()

# Create WebGPU wrapper target
if (EPX_WGPU_USE_MODULE AND EPIX_CXX_MODULE)
    # Create module target
    add_library(webgpu_wrapper STATIC)
    
    if (EXISTS "${WEBGPU_GENERATED_MODULE}")
        target_sources(webgpu_wrapper
            PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
                "${WEBGPU_GENERATED_MODULE}"
        )
    endif()
    
    target_include_directories(webgpu_wrapper PUBLIC
        ${WEBGPU_GENERATED_DIR}
    )
    
    target_link_libraries(webgpu_wrapper PUBLIC wgpu_native)
    
    # Set module properties for MSVC
    if (MSVC)
        target_compile_options(webgpu_wrapper PUBLIC /interface)
    endif()
    
    message(STATUS "WebGPU module target created: webgpu_wrapper")
else()
    # Create header-only interface target
    add_library(webgpu_wrapper INTERFACE)
    
    target_include_directories(webgpu_wrapper INTERFACE
        ${WEBGPU_GENERATED_DIR}
    )
    
    target_link_libraries(webgpu_wrapper INTERFACE wgpu_native)
    
    message(STATUS "WebGPU header-only target created: webgpu_wrapper")
endif()

# Export variables
set(WEBGPU_WRAPPER_TARGET webgpu_wrapper CACHE INTERNAL "WebGPU wrapper target name")
