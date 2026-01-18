# Fetch and setup wgpu-native precompiled binaries
# Based on WebGPU-distribution patterns

include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)

# Set default version if not specified
if (NOT DEFINED EPX_WGPU_NATIVE_VERSION)
    set(EPX_WGPU_NATIVE_VERSION "v24.0.3.1" CACHE STRING "Version of wgpu-native to fetch")
endif()

# Set binary mirror
if (NOT DEFINED EPX_WGPU_BINARY_MIRROR)
    set(EPX_WGPU_BINARY_MIRROR "https://github.com/gfx-rs/wgpu-native" CACHE STRING "Mirror for wgpu-native binaries")
endif()

# Check if we should use shared or static linking
if (NOT DEFINED EPX_WGPU_LINK_TYPE)
    set(EPX_WGPU_LINK_TYPE "SHARED" CACHE STRING "Link type for wgpu-native (SHARED or STATIC)")
endif()

set_property(CACHE EPX_WGPU_LINK_TYPE PROPERTY STRINGS SHARED STATIC)

# Detect system architecture
detect_system_architecture()

# Determine if we're using shared library
set(USE_SHARED_LIB)
if (EPX_WGPU_LINK_TYPE STREQUAL "SHARED")
    set(USE_SHARED_LIB TRUE)
elseif (EPX_WGPU_LINK_TYPE STREQUAL "STATIC")
    set(USE_SHARED_LIB FALSE)
else()
    message(FATAL_ERROR "Link type '${EPX_WGPU_LINK_TYPE}' is not valid. Possible values are SHARED and STATIC.")
endif()

# Build URL to fetch
set(URL_OS)
set(URL_COMPILER)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(URL_OS "windows")
    if (MSVC)
        set(URL_COMPILER "msvc")
    else()
        set(URL_COMPILER "gnu")
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(URL_OS "linux")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(URL_OS "macos")
else()
    message(FATAL_ERROR "Platform system '${CMAKE_SYSTEM_NAME}' not supported by wgpu-native precompiled binaries.")
endif()

set(URL_ARCH)
if (ARCH STREQUAL "x86_64")
    set(URL_ARCH "x86_64")
elseif (ARCH STREQUAL "aarch64")
    set(URL_ARCH "aarch64")
elseif (ARCH STREQUAL "i686" AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(URL_ARCH "i686")
else()
    message(FATAL_ERROR "Platform architecture '${ARCH}' not supported by wgpu-native precompiled binaries.")
endif()

# Use release builds
set(URL_CONFIG release)

# Build the URL
if (URL_COMPILER)
    set(URL_NAME "wgpu-${URL_OS}-${URL_ARCH}-${URL_COMPILER}-${URL_CONFIG}")
else()
    set(URL_NAME "wgpu-${URL_OS}-${URL_ARCH}-${URL_CONFIG}")
endif()
set(URL "${EPX_WGPU_BINARY_MIRROR}/releases/download/${EPX_WGPU_NATIVE_VERSION}/${URL_NAME}.zip")

string(TOLOWER "${URL_NAME}" FC_NAME)

# Declare and fetch content
FetchContent_Declare(${FC_NAME}
    URL ${URL}
)
message(STATUS "Fetching wgpu-native from '${URL}'")
FetchContent_MakeAvailable(${FC_NAME})
set(WGPU_NATIVE_DIR "${${FC_NAME}_SOURCE_DIR}" CACHE INTERNAL "Path to wgpu-native directory")

# Create imported target
if (USE_SHARED_LIB)
    add_library(wgpu_native SHARED IMPORTED GLOBAL)
else()
    add_library(wgpu_native STATIC IMPORTED GLOBAL)
endif()

# Set compile definitions
target_compile_definitions(wgpu_native INTERFACE WEBGPU_BACKEND_WGPU)

# Build library filename
build_lib_filename(BINARY_FILENAME "wgpu_native" ${USE_SHARED_LIB})
set(WGPU_RUNTIME_LIB "${WGPU_NATIVE_DIR}/lib/${BINARY_FILENAME}")

# Set library location
set_target_properties(wgpu_native
    PROPERTIES
        IMPORTED_LOCATION "${WGPU_RUNTIME_LIB}"
)

# Add include directories
target_include_directories(wgpu_native INTERFACE
    "${WGPU_NATIVE_DIR}/include"
    "${WGPU_NATIVE_DIR}/include/webgpu"
)

# Platform-specific settings
if (USE_SHARED_LIB)
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if (MSVC)
            set(STATIC_LIB_EXT "lib")
            set(STATIC_LIB_PREFIX "")
        else()
            set(STATIC_LIB_EXT "a")
            set(STATIC_LIB_PREFIX "lib")
        endif()

        set(WGPU_IMPLIB "${WGPU_NATIVE_DIR}/lib/${STATIC_LIB_PREFIX}${BINARY_FILENAME}.${STATIC_LIB_EXT}")
        set_target_properties(wgpu_native
            PROPERTIES
                IMPORTED_IMPLIB "${WGPU_IMPLIB}"
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(wgpu_native
            PROPERTIES
                IMPORTED_NO_SONAME TRUE
        )
    endif()

    message(STATUS "Using wgpu-native runtime from '${WGPU_RUNTIME_LIB}'")
    set(WGPU_RUNTIME_LIB ${WGPU_RUNTIME_LIB} CACHE INTERNAL "Path to wgpu-native library binary")

    # Helper function to copy binaries
    function(target_copy_wgpu_binaries Target)
        add_custom_command(
            TARGET ${Target} POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                ${WGPU_RUNTIME_LIB}
                $<TARGET_FILE_DIR:${Target}>
            COMMENT
                "Copying '${WGPU_RUNTIME_LIB}' to '$<TARGET_FILE_DIR:${Target}>'..."
        )
    endfunction()
else()
    # Static linking - add platform-specific system libraries
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(wgpu_native
            INTERFACE
                d3dcompiler.lib
                Ws2_32.lib
                Userenv.lib
                ntdll.lib
                Bcrypt.lib
                Opengl32.lib
                Propsys.lib
                RuntimeObject.lib
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(wgpu_native
            INTERFACE
                dl
                pthread
                m
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(wgpu_native
            INTERFACE
                "-framework Metal"
                "-framework QuartzCore"
                "-framework MetalKit"
        )
    endif()

    function(target_copy_wgpu_binaries Target)
        # No-op for static linking
    endfunction()
endif()
