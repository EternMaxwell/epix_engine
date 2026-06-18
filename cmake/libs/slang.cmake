# Fetch and setup Slang prebuilt binaries using Slang's own CMake package config

include(FetchContent)

# Set default version if not specified
if (NOT DEFINED EPIX_SLANG_VERSION)
    set(EPIX_SLANG_VERSION "v2026.8.1" CACHE STRING "Version of Slang to fetch")
endif()

# Detect system architecture
detect_system_architecture()

# Build download URL — version in filename drops the 'v' prefix
string(REPLACE "v" "" SLANG_VER "${EPIX_SLANG_VERSION}")

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SLANG_OS "windows")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SLANG_OS "linux")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(SLANG_OS "macos")
else()
    message(FATAL_ERROR "Platform '${CMAKE_SYSTEM_NAME}' not supported by Slang prebuilt binaries.")
endif()

set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/${EPIX_SLANG_VERSION}/slang-${SLANG_VER}-${SLANG_OS}-${ARCH}.zip")

string(TOLOWER "slang-${SLANG_VER}-${SLANG_OS}-${ARCH}" FC_NAME)

FetchContent_Declare(${FC_NAME}
    URL ${SLANG_URL}
)
message(STATUS "Fetching Slang prebuilt binaries from '${SLANG_URL}'")
FetchContent_MakeAvailable(${FC_NAME})

# Use Slang's own CMake package config — location varies by platform
set(SLANG_SOURCE_DIR "${${FC_NAME}_SOURCE_DIR}")
set(SLANG_CONFIG_FILE "" CACHE INTERNAL "Path to slangConfig.cmake file")
if (NOT SLANG_CONFIG_FILE)
  file(GLOB_RECURSE SLANG_CONFIG_FILE
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${SLANG_SOURCE_DIR}/slangConfig.cmake"
  )
endif()
if (NOT SLANG_CONFIG_FILE)
  message(FATAL_ERROR "slangConfig.cmake not found under ${SLANG_SOURCE_DIR}")
endif()
get_filename_component(slang_DIR "${SLANG_CONFIG_FILE}" DIRECTORY)
find_package(slang REQUIRED)

# Register shared libs for runtime deployment
set(EPIX_RUNTIME_SHARED_LIBS "${EPIX_RUNTIME_SHARED_LIBS};slang::slang" CACHE INTERNAL "Shared libs the engine needs at runtime")

if(EPIX_ENABLE_INSTALL)
  install(TARGETS slang::slang
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
  )
endif()
