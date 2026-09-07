include(FetchContent)

# Tonemapping LUTs use KTX2's Zstandard supercompression. Build only the
# decoder library; Epix does not need zstd's command-line tools or tests.
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG v1.5.7
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(zstd)
