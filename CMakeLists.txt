cmake_minimum_required (VERSION 3.30)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EPIX_ENABLE_TRACY "Enable tracy profiling" OFF)
option(EPIX_USE_VOLK "Use Vulkan Loader" ON)
option(EPIX_ENABLE_TEST "Enable epix test" ON)
option(EPIX_CXX_MODULE "Enable C++20 modules" ON)
option(EPIX_IMPORT_STD "Enable C++ import std" ON)

set(EPIX_IMPORT_STD ON) # Currently having issue with clang with msvc

if (EPIX_IMPORT_STD AND NOT EPIX_CXX_MODULE)
  message(WARNING "EPIX_IMPORT_STD option requires EPIX_CXX_MODULE to be enabled. Disabling EPIX_IMPORT_STD.")
  set(EPIX_IMPORT_STD OFF)
endif()

if (EPIX_IMPORT_STD)
  set(CMAKE_CXX_MODULE_STD ON)
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
endif()

project ("epix_engine.cmake" LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#sub directories
add_subdirectory (libs/glfw)
set (SPDLOG_USE_STD_FORMAT ON)
add_subdirectory (libs/spdlog)
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
add_subdirectory (libs/googletest)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# WebGPU integration
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebGPU.cmake)

set (GLM_ENABLE_CXX_20 ON)
add_subdirectory (libs/glm)
target_sources(glm
  PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/glm/glm/glm.cppm"
)

add_library(BSThreadPool STATIC)
target_include_directories(BSThreadPool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/include)
file(GLOB_RECURSE BSTHREADPOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/modules/*.cppm")
target_sources(BSThreadPool 
  PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${BSTHREADPOOL_SOURCES}
)
target_compile_definitions(BSThreadPool PRIVATE BS_THREAD_POOL_NATIVE_EXTENSIONS)
if (EPIX_IMPORT_STD)
  target_compile_definitions(BSThreadPool PRIVATE BS_THREAD_POOL_IMPORT_STD)
endif()

if (MSVC)
  add_compile_options(/bigobj)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wa,-mbig-obj)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wa,-mbig-obj)
endif ()

# Print option statuses
message(STATUS "EPIX options:")
message(STATUS "  EPIX_ENABLE_TRACY (optional): ${EPIX_ENABLE_TRACY}")
message(STATUS "  EPIX_USE_VOLK     (optional): ${EPIX_USE_VOLK}")
message(STATUS "  EPIX_ENABLE_TEST  (optional): ${EPIX_ENABLE_TEST}")
message(STATUS "  EPIX_CXX_MODULE   (optional): ${EPIX_CXX_MODULE}")
message(STATUS "  EPIX_IMPORT_STD   (optional): ${EPIX_IMPORT_STD}")

add_subdirectory (epix_engine)