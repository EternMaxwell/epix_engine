cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_physics2d STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_physics2d PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_physics2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_physics2d PUBLIC epix::app)
target_link_libraries(epix_physics2d PUBLIC box2d)
target_link_libraries(epix_physics2d PUBLIC glm::glm)
target_link_libraries(epix_physics2d PUBLIC earcut.hpp)

add_library(epix::physics::2d ALIAS epix_physics2d)