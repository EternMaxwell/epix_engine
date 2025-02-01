cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_wdsand STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_wdsand PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_wdsand PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(epix_wdsand PROPERTIES CXX_STANDARD 20)

target_link_libraries(epix_wdsand epix::app)
target_link_libraries(epix_wdsand glm::glm)
target_link_libraries(epix_wdsand epix::utils::grid)

add_library(epix::world::sand ALIAS epix_wdsand)