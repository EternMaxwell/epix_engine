cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_core_util STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_core_util PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_core_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(epix_core_util PROPERTIES CXX_STANDARD 20)
target_link_libraries(epix_core_util PUBLIC epix::common)

add_library(epix::utils::core ALIAS epix_core_util)