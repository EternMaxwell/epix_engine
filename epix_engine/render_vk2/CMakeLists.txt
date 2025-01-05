cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_rdvk2 STATIC ${MODULE_SOURCES})
target_include_directories(epix_rdvk2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_rdvk2 Vulkan::Vulkan)
target_link_libraries(epix_rdvk2 VulkanMemoryAllocator)
target_link_libraries(epix_rdvk2 glm::glm)
target_link_libraries(epix_rdvk2 epix::app)
target_link_libraries(epix_rdvk2 epix::window)

add_library(epix::render::vulkan2 ALIAS epix_rdvk2)

add_subdirectory(tests)