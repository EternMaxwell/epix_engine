cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_grid2d STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_grid2d PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_grid2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(epix_grid2d PROPERTIES CXX_STANDARD 20)

target_link_libraries(epix_grid2d PUBLIC epix::app)
target_link_libraries(epix_grid2d PUBLIC glm::glm)
target_link_libraries(epix_grid2d PUBLIC earcut.hpp)

add_library(epix::utils::grid2d ALIAS epix_grid2d)