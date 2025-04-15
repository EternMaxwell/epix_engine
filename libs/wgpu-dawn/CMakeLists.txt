cmake_minimum_required(VERSION 3.21)

set(DAWN_FETCH_DEPENDENCIES ON)
set(DAWN_USE_GLFW OFF)
set(DAWN_USE_X11 OFF)
set(DAWN_BUILD_SAMPLES OFF)
set(DAWN_BUILD_MONOLITHIC_LIBRARY OFF)
set(TINT_BUILD_CMD_TOOLS OFF)
set(TINT_BUILD_TESTS OFF)
add_subdirectory(src)

find_package(Python3 REQUIRED)
DawnJSONGenerator(TARGET "webgpu_dawn_native_proc"
                  PRINT_NAME "Dawn native WebGPU procs"
                  OUTPUT_SOURCES WEBGPU_DAWN_NATIVE_PROC_GEN_SOURCES)
add_library(webgpu STATIC ${WEBGPU_DAWN_NATIVE_PROC_GEN_SOURCES})

target_link_libraries(
    webgpu PUBLIC $<BUILD_INTERFACE:dawncpp_headers>
           PRIVATE $<LINK_ONLY:dawn::dawn_common>
           PRIVATE $<LINK_ONLY:dawn::dawn_native>
)