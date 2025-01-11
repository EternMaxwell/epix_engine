cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_rdvk2 STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_rdvk2 PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(epix_rdvk2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_rdvk2 PUBLIC Vulkan::Vulkan)
target_link_libraries(epix_rdvk2 PUBLIC VulkanMemoryAllocator)
target_link_libraries(epix_rdvk2 PUBLIC spirv-cross-glsl)
target_link_libraries(epix_rdvk2 PUBLIC glm::glm)
target_link_libraries(epix_rdvk2 PUBLIC epix::app)
target_link_libraries(epix_rdvk2 PUBLIC epix::window)

add_library(epix::render::vulkan2 ALIAS epix_rdvk2)

add_subdirectory(tests)