cmake_minimum_required (VERSION 3.30)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

message(STATUS "CMake version: ${CMAKE_VERSION}")

if (CMAKE_VERSION VERSION_LESS "3.30")
  message(WARNING "CMake version is less than 3.30, C++23 standard library modules support may be incomplete.")
else()
  if (CMAKE_VERSION VERSION_GREATER_EQUAL "4.0.3")
    set(CXX_IMPORT_STD_UUID "d0edc3af-4c50-42ea-a356-e2862fe7a444")
  elseif (CMAKE_VERSION VERSION_GREATER_EQUAL "4.0.0")
    set(CXX_IMPORT_STD_UUID "a9e1cf81-9932-4810-974b-6eccaf14e457")
  elseif (CMAKE_VERSION VERSION_GREATER_EQUAL "3.31.8")
    set(CXX_IMPORT_STD_UUID "d0edc3af-4c50-42ea-a356-e2862fe7a444")
  elseif (CMAKE_VERSION VERSION_GREATER_EQUAL "3.30.0")
    set(CXX_IMPORT_STD_UUID "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")
  endif()
  message(STATUS "CMake experimental import std uuid = ${CXX_IMPORT_STD_UUID}")
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD ${CXX_IMPORT_STD_UUID} CACHE STRING "Enable CxxImportStd experimental feature with UUID")

  cmake_language(GET_EXPERIMENTAL_FEATURE_ENABLED "CxxImportStd" _cxx_import_std_enabled)
  if (_cxx_import_std_enabled STREQUAL "TRUE")
    message(STATUS "Enabled CMake experimental feature 'CxxImportStd' using UUID: ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
  else()
    message(WARNING "Tried to enable CxxImportStd with UUID ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD} but CMake did not report it enabled.")
    # Clean up so subsequent logic doesn't assume the gate is active.
    unset(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD CACHE)
  endif()
endif()

project ("epix_engine.cmake" LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#sub directories

add_subdirectory (libs/glfw)
set (SPDLOG_USE_STD_FORMAT ON)
add_subdirectory (libs/spdlog)
set (GLM_ENABLE_CXX_20 ON)
add_subdirectory (libs/glm)
target_sources(glm
  PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/glm/glm/glm.cppm"
)
# add_subdirectory (libs/freetype)
add_subdirectory (libs/vulkan-headers)
add_subdirectory (libs/volk)
# add_subdirectory (libs/imgui)
# add_subdirectory (libs/box2d)
set (EARCUT_BUILD_TESTS OFF)
set (EARCUT_BUILD_BENCH OFF)
set (EARCUT_BUILD_VIZ OFF)
# add_subdirectory (libs/earcut)
add_subdirectory (libs/tracy)
add_subdirectory (libs/stb)
add_subdirectory (libs/uuid)
set (NVRHI_WITH_DX12 OFF)
set (NVRHI_WITH_DX11 OFF)
set (NVRHI_WITH_VULKAN ON)
add_subdirectory (libs/nvrhi)
# add_subdirectory (libs/spirv-cross)
# add_subdirectory (libs/utfcpp)
set (HARFBUZZ_HAVE_FREETYPE ON)
# add_subdirectory (libs/harfbuzz)

# vulkan hpp cppmodule setup from vulkanhpp repo
set(VULKAN_HEADER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/vulkan-headers/include")
add_library( VulkanHppModule )
target_sources( VulkanHppModule PRIVATE
  FILE_SET CXX_MODULES
  FILES ${VULKAN_HEADER_INCLUDE_DIR}/vulkan/vulkan.cppm
)
target_sources( VulkanHppModule PRIVATE 
  ${CMAKE_CURRENT_SOURCE_DIR}/libs/others/vulkan_dispatcher.cpp
)
target_compile_definitions( VulkanHppModule PRIVATE
  VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
  VULKAN_HPP_NO_EXCEPTIONS
  VULKAN_HPP_USE_STD_EXPECTED
)
target_link_libraries( VulkanHppModule PUBLIC Vulkan-Headers )

add_subdirectory (libs/googletest)

add_library(BSThreadPool STATIC)
target_include_directories(BSThreadPool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/include)
file(GLOB_RECURSE BSTHREADPOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/modules/*.cppm")
target_sources(BSThreadPool 
  PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${BSTHREADPOOL_SOURCES}
)
target_compile_definitions(BSThreadPool PRIVATE BS_THREAD_POOL_NATIVE_EXTENSIONS BS_THREAD_POOL_IMPORT_STD)

if (MSVC)
  add_compile_options(/bigobj)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wa,-mbig-obj)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wa,-mbig-obj)
endif ()

add_subdirectory (epix_engine)
