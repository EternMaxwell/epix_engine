# Only fetch and build mimalloc when examples are enabled and the user opted in.
if(NOT (EPIX_ENABLE_EXAMPLE AND EPIX_EXAMPLE_USE_MIMALLOC))
    return()
endif()

include(FetchContent)

# Build static lib only; skip shared/object variants and tests.
set(MI_BUILD_SHARED      OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC      ON  CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT      OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(MI_INSTALL_TOPLEVEL  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v3.3.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(mimalloc)
