option(EPIX_WGPU_GENERATE_ON_CONFIGURE "Generate WebGPU wrapper during CMake configure (vs build time)" ON)

set(EPIX_WGPU_NATIVE_VERSION "v25.0.2.2" CACHE STRING "Version of wgpu-native to fetch")
set(EPIX_WGPU_LINK_TYPE "STATIC" CACHE STRING "Link type for wgpu-native (SHARED or STATIC)")

include(${CMAKE_CURRENT_LIST_DIR}/wgpu_native.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/webgpu_gen_module.cmake)

# Set output directory for generated files
set(WEBGPU_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/webgpu")

# Generate WebGPU wrapper
if (EPIX_WGPU_GENERATE_ON_CONFIGURE AND WEBGPU_CPP_GENERATOR_AVAILABLE)
    # Get header files from wgpu-native
    set(WEBGPU_HEADERS
        "${WGPU_NATIVE_DIR}/include/webgpu/webgpu.h"
    )

    # Check if wgpu.h exists (backend-specific extensions)
    if (EXISTS "${WGPU_NATIVE_DIR}/include/webgpu/wgpu.h")
        list(APPEND WEBGPU_HEADERS "${WGPU_NATIVE_DIR}/include/webgpu/wgpu.h")
    endif()

    # Generate wrapper (header + source, and optionally module re-export)
    generate_webgpu_wrapper(
        OUTPUT_DIR ${WEBGPU_GENERATED_DIR}
        HEADER_FILES ${WEBGPU_HEADERS}
    )
endif()

# Create WebGPU wrapper target — always STATIC (has generated .cpp source)
add_library(webgpu STATIC)

# Generated source: out-of-class definitions, compiled once
target_sources(webgpu PRIVATE "${WEBGPU_GENERATED_DIR}/webgpu.cpp")

# Generated header: declarations and inline/template definitions
target_sources(webgpu
    PUBLIC FILE_SET HEADERS
    BASE_DIRS ${WEBGPU_GENERATED_DIR}
    FILES "${WEBGPU_GENERATED_DIR}/webgpu.hpp"
)

# Include path so that <webgpu/webgpu.hpp> resolves:
#   ${CMAKE_BINARY_DIR}/generated/webgpu/webgpu.hpp → <webgpu/webgpu.hpp>
target_include_directories(webgpu PUBLIC "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(webgpu PUBLIC wgpu_native)

# Conditionally add the module re-export
if (EPIX_CXX_MODULE)
    target_sources(webgpu
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
            "${WEBGPU_GENERATED_DIR}/webgpu.cppm"
    )
    message(STATUS "WebGPU module re-export enabled: ${WEBGPU_GENERATED_DIR}/webgpu.cppm")
endif()

message(STATUS "WebGPU target created: webgpu (header-primary, module=${EPIX_CXX_MODULE})")
