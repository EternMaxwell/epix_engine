# Fetch and configure WebGPU-Cpp generator

set(WEBGPU_CPP_GENERATOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/webgpu-wrapper" CACHE INTERNAL "Path to WebGPU-Cpp generator")
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
        ""
        "OUTPUT_DIR"
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

    # Create output directory
    file(MAKE_DIRECTORY ${GEN_OUTPUT_DIR})

    # Build header URL arguments
    set(HEADER_ARGS "")
    foreach(HEADER ${GEN_HEADER_FILES})
        list(APPEND HEADER_ARGS "-i" "${HEADER}")
    endforeach()

    # Generate regular header
    set(OUTPUT_FILE "${GEN_OUTPUT_DIR}/webgpu.cppm")
    
    # Select appropriate template
    set(TEMPLATE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/libs/webgpu-wrapper/webgpu.template.cppm")

    message(STATUS "Generating WebGPU C++ wrapper...")
    message(STATUS "  Headers: ${GEN_HEADER_FILES}")
    message(STATUS "  Output: ${OUTPUT_FILE}")
    message(STATUS "  Template: ${TEMPLATE_FILE}")

    # Generate main wrapper
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${WEBGPU_CPP_GENERATOR_SCRIPT}
            ${HEADER_ARGS}
            -t "${TEMPLATE_FILE}"
            -o "${OUTPUT_FILE}"
            --use-raii
        WORKING_DIRECTORY ${WEBGPU_CPP_GENERATOR_DIR}
        RESULT_VARIABLE GEN_RESULT
        OUTPUT_VARIABLE GEN_OUTPUT
        ERROR_VARIABLE GEN_ERROR
    )

    if (NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate WebGPU wrapper:\n${GEN_ERROR}")
    endif()

    message(STATUS "Generated: ${OUTPUT_FILE}")

    # Store output files in parent scope
    set(WEBGPU_GENERATED_MODULE "${OUTPUT_FILE}" PARENT_SCOPE)
endfunction()
