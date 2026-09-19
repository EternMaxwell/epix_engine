cmake_minimum_required(VERSION 3.21)

project(imgui LANGUAGES CXX)

file(GLOB MODULE_SOURCES
    "src/imgui.cpp"
    "src/imgui_demo.cpp"
    "src/imgui_draw.cpp"
    "src/imgui_widgets.cpp"
    "src/imgui_tables.cpp"
    "src/backends/imgui_impl_glfw.cpp"
    # "src/backends/imgui_impl_vulkan.cpp"
    "src/backends/imgui_impl_wgpu.cpp"
)
set(IMGUI_SOURCES ${IMGUI_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(IMGUI_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src PARENT_SCOPE)

# One header generation renamed WGPUProgrammableStageDescriptor to
# WGPUComputeState, gave WGPUVertexAttribute a leading `nextInChain`, and folded
# WGPUSurfaceGetCurrentTextureStatus_OutOfMemory/_DeviceLost into _Error. The
# pinned backend predates it (upstream fixed it only after 1.92.9), so the
# tracked patch is applied here, idempotently, to keep the submodule untouched.
# Drop this block and patches/ once libs/imgui/src moves past that fix.
find_package(Git QUIET)
if (Git_FOUND)
    set(IMGUI_WGPU_HEADERS_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/imgui_impl_wgpu_webgpu_headers.patch")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${IMGUI_WGPU_HEADERS_PATCH}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
        RESULT_VARIABLE IMGUI_PATCH_APPLIED
        OUTPUT_QUIET ERROR_QUIET)
    if (NOT IMGUI_PATCH_APPLIED EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${IMGUI_WGPU_HEADERS_PATCH}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
            RESULT_VARIABLE IMGUI_PATCH_RESULT
            ERROR_VARIABLE IMGUI_PATCH_ERROR)
        if (NOT IMGUI_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not apply ${IMGUI_WGPU_HEADERS_PATCH}: ${IMGUI_PATCH_ERROR}")
        endif()
        message(STATUS "Applied the imgui wgpu-backend header compatibility patch")
    endif()
else()
    message(WARNING "Git not found: the imgui wgpu-backend compatibility patch is not applied")
endif()

add_library(imgui STATIC ${MODULE_SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/backends)

target_link_libraries(imgui PRIVATE glfw)
# target_link_libraries(imgui PRIVATE Vulkan-Headers)
target_link_libraries(imgui PRIVATE webgpu)

# target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU)