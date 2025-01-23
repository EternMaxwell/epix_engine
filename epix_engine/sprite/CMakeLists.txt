cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_sprite STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_sprite PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_sprite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_include_directories(epix_sprite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/stb)

target_link_libraries(epix_sprite freetype)
target_link_libraries(epix_sprite epix::app)
target_link_libraries(epix_sprite epix::window)
target_link_libraries(epix_sprite epix::render::vulkan)
target_link_libraries(epix_sprite epix::render::vulkan2)

add_library(epix::render::sprite ALIAS epix_sprite)