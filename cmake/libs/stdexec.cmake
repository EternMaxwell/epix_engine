function(epix_stdexec_cache_default name type value doc)
  if(NOT DEFINED CACHE{${name}})
    set(${name} ${value} CACHE ${type} "${doc}")
  endif()
endfunction()

function(epix_stdexec_download_nonempty url output description)
  if(EXISTS "${output}")
    file(SIZE "${output}" size)
    if(size EQUAL 0)
      file(REMOVE "${output}")
    endif()
  endif()

  if(NOT EXISTS "${output}")
    message(STATUS "Downloading ${description}")
    file(DOWNLOAD "${url}" "${output}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    list(GET status 1 message)
    if(NOT code EQUAL 0)
      file(REMOVE "${output}")
      message(FATAL_ERROR "Failed to download ${description}: ${message}")
    endif()
  endif()

  file(SIZE "${output}" size)
  if(size EQUAL 0)
    file(REMOVE "${output}")
    message(FATAL_ERROR "Downloaded ${description}, but the file is empty")
  endif()
endfunction()

set(EPIX_STDEXEC_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/libs/stdexec")
file(MAKE_DIRECTORY "${EPIX_STDEXEC_BINARY_DIR}")
epix_stdexec_download_nonempty(
  "https://raw.githubusercontent.com/rapidsai/rapids-cmake/branch-24.02/RAPIDS.cmake"
  "${EPIX_STDEXEC_BINARY_DIR}/RAPIDS.cmake"
  "stdexec RAPIDS.cmake bootstrap")
epix_stdexec_download_nonempty(
  "https://raw.githubusercontent.com/cplusplus/sender-receiver/main/execution.bs"
  "${EPIX_STDEXEC_BINARY_DIR}/execution.bs"
  "stdexec execution.bs")

# stdexec is a dependency here; do not let its tests/examples/docs/install
# targets leak into the engine build.
set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "Build stdexec tests" FORCE)
set(STDEXEC_BUILD_RELACY_TESTS OFF CACHE BOOL "Build stdexec relacy tests" FORCE)
set(STDEXEC_BUILD_DOCS OFF CACHE BOOL "Build stdexec documentation" FORCE)
set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "Build stdexec examples" FORCE)
set(STDEXEC_INSTALL OFF CACHE BOOL "Generate stdexec install target" FORCE)

# The engine already uses standalone Asio and needs stdexec's Asio bridge.
set(STDEXEC_ENABLE_ASIO ON CACHE BOOL "Enable stdexec ASIO targets" FORCE)
set(STDEXEC_ASIO_IMPLEMENTATION standalone CACHE STRING "stdexec ASIO implementation" FORCE)

epix_stdexec_cache_default(STDEXEC_ENABLE_CUDA BOOL OFF "Enable stdexec CUDA targets")
epix_stdexec_cache_default(STDEXEC_ENABLE_TBB BOOL OFF "Enable stdexec TBB targets")
epix_stdexec_cache_default(STDEXEC_ENABLE_TASKFLOW BOOL OFF "Enable stdexec Taskflow targets")
epix_stdexec_cache_default(STDEXEC_ENABLE_NUMA BOOL OFF "Enable stdexec NUMA support")
epix_stdexec_cache_default(STDEXEC_BUILD_PARALLEL_SCHEDULER BOOL OFF "Build stdexec parallel scheduler")
epix_stdexec_cache_default(STDEXEC_ENABLE_IO_URING BOOL OFF "Enable stdexec io_uring scheduler")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/stdexec EXCLUDE_FROM_ALL)

if(TARGET asioexec AND NOT TARGET asio2stdexec)
  add_library(asio2stdexec ALIAS asioexec)
endif()
