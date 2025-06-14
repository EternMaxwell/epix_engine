﻿cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
list(APPEND ENGINE_SOURCES ${MODULE_SOURCES})
list(APPEND ENGINE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(EPIX_ENABLE_TRACY ON)
set(EPIX_WITH_SHARED OFF)

# BUILD STATIC
add_library(epix_engine STATIC ${MODULE_SOURCES})

if(EPIX_ENABLE_TRACY)
  target_compile_definitions(epix_engine PRIVATE EPIX_ENABLE_TRACY)
endif()

# THIRD PARTY LIBRARIES
target_include_directories(epix_engine PUBLIC ${ENGINE_INCLUDES})
target_link_libraries(epix_engine PUBLIC glfw)
target_link_libraries(epix_engine PUBLIC EnTT::EnTT)
target_link_libraries(epix_engine PUBLIC spdlog::spdlog)
target_link_libraries(epix_engine PUBLIC BSThreadPool)
target_link_libraries(epix_engine PUBLIC glm::glm)
target_link_libraries(epix_engine PUBLIC freetype)
target_link_libraries(epix_engine PUBLIC webgpu)
target_link_libraries(epix_engine PUBLIC earcut.hpp)
target_link_libraries(epix_engine PUBLIC TracyClient)
target_link_libraries(epix_engine PUBLIC stduuid)
target_link_libraries(epix_engine PUBLIC box2d)
target_link_libraries(epix_engine PUBLIC stbimage)
target_link_libraries(epix_engine PUBLIC volk)
target_link_libraries(epix_engine PUBLIC Vulkan-Headers)

add_library(epix ALIAS epix_engine)

# BUILD SHARED
if(EPIX_WITH_SHARED)
  add_library(epix_engine_shared SHARED ${ENGINE_SOURCES})

  if(EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_engine_shared PRIVATE EPIX_ENABLE_TRACY)
  endif()

  target_compile_definitions(epix_engine_shared PRIVATE EPIX_BUILD_SHARED)
  target_compile_definitions(epix_engine_shared PUBLIC EPIX_SHARED)
  target_include_directories(epix_engine_shared PUBLIC ${ENGINE_INCLUDES})
  target_link_libraries(epix_engine PUBLIC glfw)
  target_link_libraries(epix_engine PUBLIC EnTT::EnTT)
  target_link_libraries(epix_engine PUBLIC spdlog::spdlog)
  target_link_libraries(epix_engine PUBLIC BSThreadPool)
  target_link_libraries(epix_engine PUBLIC glm::glm)
  target_link_libraries(epix_engine PUBLIC freetype)
  target_link_libraries(epix_engine PUBLIC webgpu)
  target_link_libraries(epix_engine PUBLIC earcut.hpp)
  target_link_libraries(epix_engine PUBLIC TracyClient)
  target_link_libraries(epix_engine PUBLIC stduuid)
  target_link_libraries(epix_engine PUBLIC box2d)
  target_link_libraries(epix_engine PUBLIC stbimage)
  target_link_libraries(epix_engine PUBLIC volk)
  target_link_libraries(epix_engine PUBLIC Vulkan-Headers)

  set_target_properties(epix_engine_shared PROPERTIES OUTPUT_NAME epix)

  add_library(epix_shared ALIAS epix_engine_shared)
endif()
