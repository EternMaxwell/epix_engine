cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(pixel_engine_vulkan_graph STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(pixel_engine_vulkan_graph PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(pixel_engine_vulkan_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(pixel_engine_vulkan_graph EnTT::EnTT)
target_link_libraries(pixel_engine_vulkan_graph BSThreadPool)
target_link_libraries(pixel_engine_vulkan_graph spdlog::spdlog)

add_library(pixel_engine::vulkan_graph ALIAS pixel_engine_vulkan_graph)
add_library(epix::vulkan_graph ALIAS pixel_engine_vulkan_graph)
