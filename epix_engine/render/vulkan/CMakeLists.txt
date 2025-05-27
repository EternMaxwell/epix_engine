cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_vulkan STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_vulkan PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_vulkan PUBLIC epix::app)
target_link_libraries(epix_vulkan PUBLIC volk)
target_link_libraries(epix_vulkan PUBLIC Vulkan-Headers)

add_library(epix::vulkan ALIAS epix_vulkan)