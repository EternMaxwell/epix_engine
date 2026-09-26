cmake_minimum_required (VERSION 3.30)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
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
option(EPIX_ENABLE_SHARED "Build epix_engine as a shared library (OFF builds a static library)" ON)
option(EPIX_ENABLE_INSTALL "Enable install rules (for packaging/distribution)" OFF)
option(EPIX_EXAMPLE_USE_MIMALLOC "Link mimalloc (static) into all examples" ON)

include(cmake/module-std.cmake)

project ("epix_engine.cmake" LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/utils.cmake)
include(cmake/libs.cmake)

set(CMAKE_CXX_SCAN_FOR_MODULES ${EPIX_CXX_MODULE})

if (MSVC)
  message(STATUS "Applying MSVC-specific compile options")
  add_compile_options(/bigobj)
  add_compile_options(/wd4819)
  add_compile_options(/wd5244)
  # CMAKE_DEPFILE_FLAGS_C/CXX keep /showIncludes: Ninja's `msvc_deps_prefix`
  # parsing is what records each object's header dependencies, so removing the
  # flag leaves every object with zero recorded deps and lets an incremental
  # build skip header consumers entirely (a mixed-ABI binary).
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(STATUS "Applying Clang-specific compile options")
  add_compile_options(-Wno-unknown-attributes)
  add_compile_options(-Wno-include-angled-in-module-purview)
  add_compile_options(-Wno-unused-value)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(STATUS "Applying GCC-specific compile options")
  # add_compile_options(-Wa,-mbig-obj)
endif ()

# Print option statuses
message(STATUS "EPIX options:")
message(STATUS "  EPIX_ENABLE_TRACY         (optional): ${EPIX_ENABLE_TRACY}")
message(STATUS "  EPIX_USE_VOLK             (optional): ${EPIX_USE_VOLK}")
message(STATUS "  EPIX_ENABLE_TEST          (optional): ${EPIX_ENABLE_TEST}")
message(STATUS "  EPIX_ENABLE_SHARED        (optional): ${EPIX_ENABLE_SHARED}")
message(STATUS "  EPIX_CXX_MODULE           (optional): ${EPIX_CXX_MODULE}")
message(STATUS "  EPIX_IMPORT_STD           (optional): ${EPIX_IMPORT_STD}")
message(STATUS "  EPIX_EXAMPLE_USE_MIMALLOC (optional): ${EPIX_EXAMPLE_USE_MIMALLOC}")

# Output directory defaults — set here (not inside the third-party library
# setups) so that the engine, the tests and the examples all share them while
# fetched dependencies keep their own defaults.
if (NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()
if (NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()
if (NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# Engine source root — always the directory of this CMakeLists.txt, even when
# epix_engine is used as an add_subdirectory() target. Used as WORKING_DIRECTORY
# for all engine tests so that "assets/..." paths resolve correctly.
set(EPIX_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Root source directory of the epix engine (this CMakeLists.txt's directory)")

if (EPIX_ENABLE_TEST)
  enable_testing()
endif()

# ── The single engine library ─────────────────────────────────────────────
# There is exactly one engine target. Logical modules contribute their sources
# and include directories to it (see epix_engine/CMakeLists.txt); what a
# translation unit may import or include is decided in the source files, not by
# link lines between module libraries. The target itself is created in
# epix_engine/CMakeLists.txt so that it shares a directory scope with the
# engine's generated sources.
set(EPIX_ENGINE_TARGET epix_engine)

if(EPIX_ENABLE_SHARED AND EPIX_ENABLE_EXAMPLE AND EPIX_EXAMPLE_USE_MIMALLOC)
  message(STATUS "  EPIX_EXAMPLE_USE_MIMALLOC is ignored while EPIX_ENABLE_SHARED=ON: a static mimalloc in an example would own a different heap than the engine DLL")
endif()

add_subdirectory (epix_engine)
add_subdirectory (tests)
add_subdirectory (examples)
