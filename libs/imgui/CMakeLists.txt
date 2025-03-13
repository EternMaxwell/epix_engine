cmake_minimum_required(VERSION 3.21)

project(imgui LANGUAGES CXX)

file(GLOB MODULE_SOURCES
    "imgui.cpp"
    "imgui_demo.cpp"
    "imgui_draw.cpp"
    "imgui_widgets.cpp"
    "imgui_tables.cpp"
    "backends/imgui_impl_glfw.cpp"
    "backends/imgui_impl_opengl3.cpp"
    "backends/imgui_impl_vulkan.cpp"
)
set(IMGUI_SOURCES ${IMGUI_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(IMGUI_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)

add_library(imgui STATIC ${MODULE_SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(imgui PRIVATE glfw)
target_link_libraries(imgui PRIVATE OpenGL::GL)
target_link_libraries(imgui PRIVATE volk)

target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)