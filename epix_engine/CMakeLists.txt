﻿cmake_minimum_required (VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
list(APPEND ENGINE_SOURCES ${MODULE_SOURCES})
list(APPEND ENGINE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(EPIX_ENABLE_TRACY ON)

# EPIX CORE MODULES
add_subdirectory(common)
add_subdirectory(app)
add_subdirectory(window)
add_subdirectory(input)
# EPIX RENDER MODULES
add_subdirectory(vulkan)
add_subdirectory(render_ogl)
add_subdirectory(vulkan_graph)
add_subdirectory(render/vulkan)
add_subdirectory(imgui)
add_subdirectory(render/dbgrd)
add_subdirectory(render/font)
add_subdirectory(render/sprite)
add_subdirectory(render/pixel)
# EPIX UTILS MODULES
add_subdirectory(utils/grid)
# EPIX WORLD MODULES
add_subdirectory(world/sand)
add_subdirectory(world/pixel_b2d)
add_subdirectory(world/sync/sand2b2d)
add_subdirectory(world/sync/b2d2sand)

# BUILD STATIC
add_library(epix_engine STATIC ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_engine PRIVATE EPIX_ENABLE_TRACY)
endif()
# THIRD PARTY LIBRARIES
target_include_directories(epix_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(epix_engine PUBLIC ../libs/stb)
target_link_libraries (epix_engine PUBLIC glfw)
target_link_libraries (epix_engine PUBLIC OpenGL::GL)
target_link_libraries (epix_engine PUBLIC glad)
target_link_libraries (epix_engine PUBLIC Vulkan::Vulkan)
target_link_libraries (epix_engine PUBLIC EnTT::EnTT)
target_link_libraries (epix_engine PUBLIC spdlog::spdlog)
target_link_libraries (epix_engine PUBLIC Boost::pfr)
target_link_libraries (epix_engine PUBLIC BSThreadPool)
target_link_libraries (epix_engine PUBLIC glm::glm)
target_link_libraries (epix_engine PUBLIC freetype)
target_link_libraries (epix_engine PUBLIC VulkanMemoryAllocator)
target_link_libraries (epix_engine PUBLIC earcut.hpp)
target_link_libraries (epix_engine PUBLIC TracyClient)
# EPIX CORE LIBRARIES
target_link_libraries (epix_engine PUBLIC epix_common)
target_link_libraries (epix_engine PUBLIC epix::app)
target_link_libraries (epix_engine PUBLIC epix::window)
target_link_libraries (epix_engine PUBLIC epix::input)
# EPIX RENDER LIBRARIES
target_link_libraries (epix_engine PUBLIC epix::render::vulkan)
target_link_libraries (epix_engine PUBLIC epix::render::debug)
target_link_libraries (epix_engine PUBLIC epix::imgui)
target_link_libraries (epix_engine PUBLIC epix::render::font)
target_link_libraries (epix_engine PUBLIC epix::render::sprite)
target_link_libraries (epix_engine PUBLIC epix::render::pixel)
# EPIX UTILS LIBRARIES
target_link_libraries (epix_engine PUBLIC epix::utils::grid)
# EPIX WORLD LIBRARIES
target_link_libraries (epix_engine PUBLIC epix::world::sand)
target_link_libraries (epix_engine PUBLIC epix::world::pixel_b2d)
target_link_libraries (epix_engine PUBLIC epix::world::sync::sand2b2d)
target_link_libraries (epix_engine PUBLIC epix::world::sync::b2d2sand)
# EPIX ALIAS LIBRARY
add_library(epix ALIAS epix_engine)

# BUILD SHARED
# ADD IMGUI SOURCES AND INCLUDES
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${IMGUI_SOURCES})
add_library(epix_engine_shared SHARED ${ENGINE_SOURCES})
if (EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_engine_shared PRIVATE EPIX_ENABLE_TRACY)
endif()
target_compile_definitions(epix_engine_shared PRIVATE EPIX_BUILD_SHARED)
target_include_directories(epix_engine_shared PUBLIC ${ENGINE_INCLUDES})
target_include_directories(epix_engine_shared PUBLIC ../libs/stb)
target_link_libraries (epix_engine_shared PUBLIC imgui)
target_link_libraries (epix_engine_shared PUBLIC glfw)
target_link_libraries (epix_engine_shared PUBLIC OpenGL::GL)
target_link_libraries (epix_engine_shared PUBLIC glad)
target_link_libraries (epix_engine_shared PUBLIC Vulkan::Vulkan)
target_link_libraries (epix_engine_shared PUBLIC EnTT::EnTT)
target_link_libraries (epix_engine_shared PUBLIC spdlog::spdlog)
target_link_libraries (epix_engine_shared PUBLIC Boost::pfr)
target_link_libraries (epix_engine_shared PUBLIC BSThreadPool)
target_link_libraries (epix_engine_shared PUBLIC glm::glm)
target_link_libraries (epix_engine_shared PUBLIC freetype)
target_link_libraries (epix_engine_shared PUBLIC VulkanMemoryAllocator)
target_link_libraries (epix_engine_shared PUBLIC earcut.hpp)
target_link_libraries (epix_engine_shared PUBLIC box2d)
target_link_libraries (epix_engine_shared PUBLIC TracyClient)
target_link_libraries (epix_engine_shared PUBLIC spirv-cross-glsl)

set_target_properties(epix_engine_shared PROPERTIES OUTPUT_NAME epix)

add_library(epix_shared ALIAS epix_engine_shared)
