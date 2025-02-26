cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_font STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_font PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_font PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_font freetype)
target_link_libraries(epix_font Vulkan::Vulkan)
target_link_libraries(epix_font glfw)
target_link_libraries(epix_font epix::app)
target_link_libraries(epix_font epix::window)
target_link_libraries(epix_font epix::render::ogl)
target_link_libraries(epix_font epix::render::vulkan)

add_library(epix::render::font ALIAS epix_font)

add_subdirectory(tests)