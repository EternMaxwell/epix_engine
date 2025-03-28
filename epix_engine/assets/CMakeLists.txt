cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_assets STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_assets PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_assets PUBLIC epix_common)
target_link_libraries(epix_assets PUBLIC index_utilities)
target_link_libraries(epix_assets PUBLIC spdlog::spdlog)

add_library(epix::assets ALIAS epix_assets)

add_subdirectory(tests)
