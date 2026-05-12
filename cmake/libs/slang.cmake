# Fetch and setup Slang prebuilt binaries

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
set(SLANG_DIR "${${FC_NAME}_SOURCE_DIR}" CACHE INTERNAL "Path to Slang prebuilt directory")

# Create imported shared library target
add_library(slang SHARED IMPORTED GLOBAL)

# Build library filename
build_lib_filename(SLANG_LIB "slang" TRUE)
set(SLANG_RUNTIME_LIB "${SLANG_DIR}/lib/${SLANG_LIB}")

set_target_properties(slang PROPERTIES
    IMPORTED_LOCATION "${SLANG_RUNTIME_LIB}"
)

target_include_directories(slang INTERFACE "${SLANG_DIR}/include")
target_compile_definitions(slang INTERFACE SLANG_SHARED_LIBRARY)

# Platform-specific settings
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (MSVC)
        set(SLANG_IMPLIB "${SLANG_DIR}/lib/slang.lib")
    else()
        set(SLANG_IMPLIB "${SLANG_DIR}/lib/libslang.dll.a")
    endif()
    set_target_properties(slang PROPERTIES
        IMPORTED_IMPLIB "${SLANG_IMPLIB}"
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(slang PROPERTIES
        IMPORTED_NO_SONAME TRUE
    )
endif()

message(STATUS "Using Slang runtime from '${SLANG_RUNTIME_LIB}'")
set(SLANG_RUNTIME_LIB ${SLANG_RUNTIME_LIB} CACHE INTERNAL "Path to Slang shared library")

# Expose slangc executable path for standard-module compilation at build time
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SLANGC_EXECUTABLE "${SLANG_DIR}/bin/slangc.exe" CACHE FILEPATH "Path to slangc compiler")
else()
    set(SLANGC_EXECUTABLE "${SLANG_DIR}/bin/slangc" CACHE FILEPATH "Path to slangc compiler")
endif()

set(EPIX_RUNTIME_SHARED_LIBS "${EPIX_RUNTIME_SHARED_LIBS};slang" CACHE INTERNAL "Shared libs the engine needs at runtime")

# Compatibility alias for existing targets that link slang_compiler
if(NOT TARGET slang_compiler)
    add_library(slang_compiler ALIAS slang)
endif()

if(EPIX_ENABLE_INSTALL)
  install(TARGETS slang
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
  )
endif()
