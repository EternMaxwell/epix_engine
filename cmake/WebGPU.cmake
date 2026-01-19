option(EPIX_WGPU_GENERATE_ON_CONFIGURE "Generate WebGPU wrapper during CMake configure (vs build time)" ON)

set(EPIX_WGPU_NATIVE_VERSION "v27.0.4.0" CACHE STRING "Version of wgpu-native to fetch")
set(EPIX_WGPU_LINK_TYPE "STATIC" CACHE STRING "Link type for wgpu-native (SHARED or STATIC)")

include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FetchWgpuNative.cmake)
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

    # Generate wrapper
    generate_webgpu_wrapper(
        OUTPUT_DIR ${WEBGPU_GENERATED_DIR}
        HEADER_FILES ${WEBGPU_HEADERS}
    )
endif()

# Create WebGPU wrapper target
add_library(webgpu STATIC)
target_sources(webgpu
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/webgpu/webgpu.cppm"
)
target_link_libraries(webgpu PUBLIC wgpu_native)
message(STATUS "WebGPU module target created: webgpu")