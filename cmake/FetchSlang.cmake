# Build Slang from source as a static library
# Provides the 'slang_compiler' ALIAS target for linking.

include(FetchContent)

# Set default version if not specified
if (NOT DEFINED EPIX_SLANG_VERSION)
    set(EPIX_SLANG_VERSION "v2026.5.2" CACHE STRING "Version of Slang to fetch")
endif()

# ── Configure Slang build options BEFORE fetching ────────────────────────────
# Build as a static library so no DLL deployment is needed.
set(SLANG_LIB_TYPE STATIC CACHE STRING "" FORCE)
set(SLANG_EMBED_CORE_MODULE TRUE CACHE BOOL "" FORCE)
set(SLANG_EMBED_CORE_MODULE_SOURCE FALSE CACHE BOOL "" FORCE)

# Disable features we don't need — keeps build fast.
set(SLANG_ENABLE_GFX OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "" FORCE)
# Keep slangc enabled: standard-modules CMake falls back to PATH slangc when this is OFF.
set(SLANG_ENABLE_SLANGC ON CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGRT OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "" FORCE)
set(SLANG_SLANG_LLVM_FLAVOR DISABLE CACHE STRING "" FORCE)
set(SLANG_EXCLUDE_DAWN ON CACHE BOOL "" FORCE)
set(SLANG_EXCLUDE_TINT ON CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_PREBUILT_BINARIES OFF CACHE BOOL "" FORCE)

# ── Fetch Slang source ──────────────────────────────────────────────────────
# Only fetch the submodules that are unconditionally required by the compiler
# (we've disabled GFX, glslang, tests, examples, etc.).
FetchContent_Declare(slang
    GIT_REPOSITORY https://github.com/shader-slang/slang.git
    GIT_TAG        ${EPIX_SLANG_VERSION}
    GIT_SHALLOW    ON
    GIT_SUBMODULES
        external/unordered_dense
        external/miniz
        external/lz4
        external/lua
        external/cmark
        external/spirv-headers
        external/vulkan
)

message(STATUS "Fetching Slang ${EPIX_SLANG_VERSION} from source (static build)...")
FetchContent_MakeAvailable(slang)

# ── Work around MSVC codepage warning in cmark (C4819) ──────────────────────
# cmark uses /WX, and on non-UTF-8 MSVC locales C4819 fires → build error.
if(MSVC AND TARGET libcmark-gfm)
    target_compile_options(libcmark-gfm PRIVATE /wd4819)
endif()

# ── Provide a consistent target name for the rest of the project ─────────────
# Slang's build produces the 'slang' target. We alias it so epix_shader can
# link PUBLIC slang_compiler without caring about build-from-source details.
if(NOT TARGET slang_compiler)
    add_library(slang_compiler ALIAS slang)
endif()
