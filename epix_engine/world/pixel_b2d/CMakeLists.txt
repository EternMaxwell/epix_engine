cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_pixel_b2d STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_pixel_b2d PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_pixel_b2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_pixel_b2d PUBLIC epix::app)
target_link_libraries(epix_pixel_b2d PUBLIC box2d)
target_link_libraries(epix_pixel_b2d PUBLIC glm::glm)
target_link_libraries(epix_pixel_b2d PUBLIC earcut.hpp)
target_link_libraries(epix_pixel_b2d PUBLIC epix::utils::grid)
target_link_libraries(epix_pixel_b2d PUBLIC epix::world::sand)

add_library(epix::world::pixel_b2d ALIAS epix_pixel_b2d)