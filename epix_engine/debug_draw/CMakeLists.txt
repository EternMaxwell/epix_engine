cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_rdbg STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_rdbg PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_rdbg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_rdbg epix::app)
target_link_libraries(epix_rdbg epix::window)
target_link_libraries(epix_rdbg epix::render::vulkan)

add_library(epix::render::debug ALIAS epix_rdbg)