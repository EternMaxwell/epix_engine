# Fetch and configure WebGPU-Cpp generator
# This fetches the generator and creates targets for generating WebGPU C++ wrapper

include(FetchContent)

# Fetch WebGPU-Cpp repository
if (NOT DEFINED EPX_WEBGPU_CPP_REPO)
    set(EPX_WEBGPU_CPP_REPO "https://github.com/EternMaxwell/WebGPU-Cpp.git" CACHE STRING "WebGPU-Cpp repository URL")
endif()

if (NOT DEFINED EPX_WEBGPU_CPP_TAG)
    set(EPX_WEBGPU_CPP_TAG "main" CACHE STRING "WebGPU-Cpp repository tag/branch")
endif()

FetchContent_Declare(webgpu_cpp_generator
    GIT_REPOSITORY ${EPX_WEBGPU_CPP_REPO}
    GIT_TAG ${EPX_WEBGPU_CPP_TAG}
    GIT_SHALLOW TRUE
)

message(STATUS "Fetching WebGPU-Cpp generator from ${EPX_WEBGPU_CPP_REPO}")
FetchContent_MakeAvailable(webgpu_cpp_generator)

set(WEBGPU_CPP_GENERATOR_DIR "${webgpu_cpp_generator_SOURCE_DIR}" CACHE INTERNAL "Path to WebGPU-Cpp generator")
set(WEBGPU_CPP_GENERATOR_SCRIPT "${WEBGPU_CPP_GENERATOR_DIR}/generate.py" CACHE INTERNAL "Path to generator script")

# Check if Python is available
find_package(Python3 COMPONENTS Interpreter)
if (NOT Python3_FOUND)
    message(WARNING "Python3 not found. WebGPU-Cpp generator will not be available.")
    set(WEBGPU_CPP_GENERATOR_AVAILABLE FALSE CACHE INTERNAL "Whether WebGPU-Cpp generator is available")
else()
    set(WEBGPU_CPP_GENERATOR_AVAILABLE TRUE CACHE INTERNAL "Whether WebGPU-Cpp generator is available")
    message(STATUS "Python3 found: ${Python3_EXECUTABLE}")
endif()

# Function to generate WebGPU wrapper
# Parameters:
#   OUTPUT_DIR - Directory where generated files will be placed
#   HEADER_FILES - List of WebGPU header files to process
#   GENERATE_MODULE - Whether to generate C++20 module (ON/OFF)
#   MODULE_NAME - Name of the module (default: webgpu)
function(generate_webgpu_wrapper)
    cmake_parse_arguments(
        GEN
        "GENERATE_MODULE"
        "OUTPUT_DIR;MODULE_NAME"
        "HEADER_FILES"
        ${ARGN}
    )

    if (NOT WEBGPU_CPP_GENERATOR_AVAILABLE)
        message(FATAL_ERROR "Cannot generate WebGPU wrapper: Python3 not found")
    endif()

    if (NOT DEFINED GEN_OUTPUT_DIR)
        message(FATAL_ERROR "OUTPUT_DIR is required for generate_webgpu_wrapper")
    endif()

    if (NOT DEFINED GEN_HEADER_FILES)
        message(FATAL_ERROR "HEADER_FILES is required for generate_webgpu_wrapper")
    endif()

    if (NOT DEFINED GEN_MODULE_NAME)
        set(GEN_MODULE_NAME "webgpu")
    endif()

    # Create output directory
    file(MAKE_DIRECTORY ${GEN_OUTPUT_DIR})

    # Build header URL arguments
    set(HEADER_ARGS "")
    foreach(HEADER ${GEN_HEADER_FILES})
        list(APPEND HEADER_ARGS "-u" "${HEADER}")
    endforeach()

    # Generate regular header
    set(OUTPUT_HEADER "${GEN_OUTPUT_DIR}/webgpu.hpp")
    set(OUTPUT_RAII_HEADER "${GEN_OUTPUT_DIR}/webgpu-raii.hpp")
    
    # Select appropriate template
    if (GEN_GENERATE_MODULE)
        set(TEMPLATE_FILE "${WEBGPU_CPP_GENERATOR_DIR}/webgpu.template.hpp")
    else()
        set(TEMPLATE_FILE "${WEBGPU_CPP_GENERATOR_DIR}/webgpu.template.hpp")
    endif()

    message(STATUS "Generating WebGPU C++ wrapper...")
    message(STATUS "  Headers: ${GEN_HEADER_FILES}")
    message(STATUS "  Output: ${OUTPUT_HEADER}")
    message(STATUS "  Template: ${TEMPLATE_FILE}")

    # Generate main wrapper
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${WEBGPU_CPP_GENERATOR_SCRIPT}
            ${HEADER_ARGS}
            -t "${TEMPLATE_FILE}"
            -o "${OUTPUT_HEADER}"
            --use-init-macros
        WORKING_DIRECTORY ${WEBGPU_CPP_GENERATOR_DIR}
        RESULT_VARIABLE GEN_RESULT
        OUTPUT_VARIABLE GEN_OUTPUT
        ERROR_VARIABLE GEN_ERROR
    )

    if (NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate WebGPU wrapper:\n${GEN_ERROR}")
    endif()

    message(STATUS "Generated: ${OUTPUT_HEADER}")

    # Generate RAII wrapper
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${WEBGPU_CPP_GENERATOR_SCRIPT}
            ${HEADER_ARGS}
            -t "${WEBGPU_CPP_GENERATOR_DIR}/webgpu-raii.template.hpp"
            -o "${OUTPUT_RAII_HEADER}"
            --use-init-macros
        WORKING_DIRECTORY ${WEBGPU_CPP_GENERATOR_DIR}
        RESULT_VARIABLE GEN_RESULT
        OUTPUT_VARIABLE GEN_OUTPUT
        ERROR_VARIABLE GEN_ERROR
    )

    if (NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate WebGPU RAII wrapper:\n${GEN_ERROR}")
    endif()

    message(STATUS "Generated: ${OUTPUT_RAII_HEADER}")

    # Store output files in parent scope
    set(WEBGPU_GENERATED_HEADER "${OUTPUT_HEADER}" PARENT_SCOPE)
    set(WEBGPU_GENERATED_RAII_HEADER "${OUTPUT_RAII_HEADER}" PARENT_SCOPE)

    # If module generation is requested, create a .cppm file
    if (GEN_GENERATE_MODULE)
        set(OUTPUT_MODULE "${GEN_OUTPUT_DIR}/${GEN_MODULE_NAME}.cppm")
        
        # For now, create a simple module wrapper around the header
        # This is a basic implementation that can be enhanced later
        file(WRITE "${OUTPUT_MODULE}"
"// Generated C++20 module interface for WebGPU
// This wraps the WebGPU-Cpp header as a module

module;

#define WEBGPU_CPP_IMPLEMENTATION
#include \"webgpu.hpp\"

export module ${GEN_MODULE_NAME};

export namespace wgpu {
    using namespace ::wgpu;
}
")
        message(STATUS "Generated: ${OUTPUT_MODULE}")
        set(WEBGPU_GENERATED_MODULE "${OUTPUT_MODULE}" PARENT_SCOPE)
    endif()
endfunction()
