cmake_minimum_required(VERSION 3.21)

project(imgui LANGUAGES CXX)

file(GLOB MODULE_SOURCES
    "src/imgui.cpp"
    "src/imgui_demo.cpp"
    "src/imgui_draw.cpp"
    "src/imgui_widgets.cpp"
    "src/imgui_tables.cpp"
    "src/backends/imgui_impl_glfw.cpp"
    "src/backends/imgui_impl_wgpu.cpp"
    "src/backends/imgui_impl_vulkan.cpp"
)
set(IMGUI_SOURCES ${IMGUI_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(IMGUI_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)

add_library(imgui STATIC ${MODULE_SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/backends)

target_link_libraries(imgui PRIVATE glfw)
target_link_libraries(imgui PRIVATE webgpu)
target_link_libraries(imgui PRIVATE Vulkan-Headers)

target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU)