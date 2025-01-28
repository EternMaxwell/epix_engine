cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_imgui STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_imgui PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_imgui epix::app)
target_link_libraries(epix_imgui epix::window)
target_link_libraries(epix_imgui epix::render::ogl)
target_link_libraries(epix_imgui epix::render::vulkan)
target_link_libraries(epix_imgui epix::render::vulkan2)
target_link_libraries(epix_imgui imgui)

add_library(epix::imgui ALIAS epix_imgui)

add_subdirectory(tests)