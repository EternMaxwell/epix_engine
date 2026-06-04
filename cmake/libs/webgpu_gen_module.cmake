# Fetch and configure WebGPU-Cpp generator

set(WEBGPU_CPP_GENERATOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/webgpu-wrapper" CACHE INTERNAL "Path to WebGPU-Cpp generator")
set(WEBGPU_CPP_GENERATOR_SCRIPT "${WEBGPU_CPP_GENERATOR_DIR}/generate.py" CACHE INTERNAL "Path to generator script")

# Template paths (in the main project, NOT in the submodule)
set(WEBGPU_CPP_HEADER_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/webgpu.template.hpp" CACHE INTERNAL "Path to header template")
set(WEBGPU_CPP_SOURCE_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/webgpu.template.cpp" CACHE INTERNAL "Path to source template")
set(WEBGPU_CPP_MODULE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_webgpu_module.py" CACHE INTERNAL "Path to module re-export generator")

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

    # Output files
    set(OUTPUT_HEADER "${GEN_OUTPUT_DIR}/webgpu.hpp")
    set(OUTPUT_SOURCE "${GEN_OUTPUT_DIR}/webgpu.cpp")
    set(OUTPUT_MODULE "${GEN_OUTPUT_DIR}/webgpu.cppm")

    message(STATUS "Generating WebGPU C++ wrapper...")
    message(STATUS "  Headers: ${GEN_HEADER_FILES}")
    message(STATUS "  Output header: ${OUTPUT_HEADER}")
    message(STATUS "  Output source: ${OUTPUT_SOURCE}")

    # Generate the header at configure time (always needed, header-only library)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${WEBGPU_CPP_GENERATOR_SCRIPT}
            ${HEADER_ARGS}
            -t "${WEBGPU_CPP_HEADER_TEMPLATE}"
            -o "${OUTPUT_HEADER}"
            --use-raii
            --indexed-handle BindGroupLayout
        WORKING_DIRECTORY ${WEBGPU_CPP_GENERATOR_DIR}
        RESULT_VARIABLE GEN_RESULT
        OUTPUT_VARIABLE GEN_OUTPUT
        ERROR_VARIABLE GEN_ERROR
    )
    if (NOT GEN_RESULT EQUAL 0)
        message(WARNING "WebGPU header generation failed: ${GEN_ERROR}")
    endif()

    # Generate the source file (out-of-class definitions) at configure time
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${WEBGPU_CPP_GENERATOR_SCRIPT}
            ${HEADER_ARGS}
            -t "${WEBGPU_CPP_SOURCE_TEMPLATE}"
            -o "${OUTPUT_SOURCE}"
            --use-raii
            --indexed-handle BindGroupLayout
        WORKING_DIRECTORY ${WEBGPU_CPP_GENERATOR_DIR}
        RESULT_VARIABLE GEN_SRC_RESULT
        ERROR_VARIABLE GEN_SRC_ERROR
    )
    if (NOT GEN_SRC_RESULT EQUAL 0)
        message(WARNING "WebGPU source generation failed: ${GEN_SRC_ERROR}")
    endif()

    # Generate the module re-export at build time (depends on the header)
    add_custom_command(
        OUTPUT ${OUTPUT_MODULE}
        COMMAND ${Python3_EXECUTABLE} ${WEBGPU_CPP_MODULE_SCRIPT}
            -i "${OUTPUT_HEADER}"
            -o "${OUTPUT_MODULE}"
            -n "webgpu"
        DEPENDS ${OUTPUT_HEADER} ${WEBGPU_CPP_MODULE_SCRIPT}
        COMMENT "Generating WebGPU module re-export..."
        VERBATIM
    )

    message(STATUS "Output module: ${OUTPUT_MODULE}")

    # Store output files in parent scope
    set(WEBGPU_GENERATED_HEADER "${OUTPUT_HEADER}" PARENT_SCOPE)
    set(WEBGPU_GENERATED_SOURCE "${OUTPUT_SOURCE}" PARENT_SCOPE)
    set(WEBGPU_GENERATED_MODULE "${OUTPUT_MODULE}" PARENT_SCOPE)
endfunction()
