cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_b2d2sand STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_b2d2sand PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_b2d2sand PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_b2d2sand PUBLIC epix::app)
target_link_libraries(epix_b2d2sand PUBLIC epix::world::sand)
target_link_libraries(epix_b2d2sand PUBLIC epix::world::pixel_b2d)

add_library(epix::world::sync::b2d2sand ALIAS epix_b2d2sand)