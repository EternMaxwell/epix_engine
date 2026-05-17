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
option(EPIX_ENABLE_EXAMPLE "Enable epix examples" ON)
option(EPIX_CXX_MODULE "Enable C++20 modules" ON)
option(EPIX_IMPORT_STD "Enable C++ import std" ON)
option(EPIX_ENABLE_INSTALL "Enable install rules (for packaging/distribution)" OFF)
option(EPIX_EXAMPLE_USE_MIMALLOC "Link mimalloc (static) into all examples" ON)

if (NOT EPIX_CXX_MODULE)
  message(FATAL_ERROR "EPIX_CXX_MODULE option is mandatory for this project and must be enabled.")
endif()

include(cmake/module-std.cmake)

project ("epix_engine.cmake" LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/utils.cmake)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(cmake/libs.cmake)

if (MSVC)
  add_compile_options(/bigobj)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # add_compile_options(-Wa,-mbig-obj)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # add_compile_options(-Wa,-mbig-obj)
endif ()

# Print option statuses
message(STATUS "EPIX options:")
message(STATUS "  EPIX_ENABLE_TRACY         (optional): ${EPIX_ENABLE_TRACY}")
message(STATUS "  EPIX_USE_VOLK             (optional): ${EPIX_USE_VOLK}")
message(STATUS "  EPIX_ENABLE_TEST          (optional): ${EPIX_ENABLE_TEST}")
message(STATUS "  EPIX_CXX_MODULE           (optional): ${EPIX_CXX_MODULE}")
message(STATUS "  EPIX_IMPORT_STD           (optional): ${EPIX_IMPORT_STD}")
message(STATUS "  EPIX_EXAMPLE_USE_MIMALLOC (optional): ${EPIX_EXAMPLE_USE_MIMALLOC}")

# Engine source root — always the directory of this CMakeLists.txt, even when
# epix_engine is used as an add_subdirectory() target. Used as WORKING_DIRECTORY
# for all engine tests so that "assets/..." paths resolve correctly.
set(EPIX_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Root source directory of the epix engine (this CMakeLists.txt's directory)")

if (EPIX_ENABLE_TEST)
  enable_testing()
endif()

add_subdirectory (epix_engine)
