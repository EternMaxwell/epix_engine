include(FetchContent)

FetchContent_Declare(
asio
GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
GIT_TAG asio-1-38-0
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
# asio/include is a symlink -> ../include; resolve for systems without symlink support
if(IS_DIRECTORY "${asio_SOURCE_DIR}/asio/include/asio")
  target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
else()
  target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/include)
endif()
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

# Platform-specific ASIO setup: async file I/O (asio::stream_file / ASIO_HAS_FILE).
#
# Windows : Links ws2_32; WIN32_LEAN_AND_MEAN prevents winsock.h/winsock2.h conflicts.
#           ASIO auto-detects IOCP → ASIO_HAS_WINDOWS_RANDOM_ACCESS_HANDLE → ASIO_HAS_FILE.
# Linux   : Async file I/O requires io_uring (kernel >= 5.10) + liburing.
#           Install liburing-dev / liburing-devel to enable; falls back to sync I/O otherwise.
# macOS / BSD : No native async file I/O for regular files; synchronous fallback used.
if(WIN32)
  # WIN32_LEAN_AND_MEAN avoids <windows.h> pulling in winsock.h before winsock2.h,
  # which would cause redefinition errors when ASIO includes winsock2.h.
  target_compile_definitions(asio INTERFACE WIN32_LEAN_AND_MEAN)
  # ws2_32: ASIO's io_context calls WSAStartup during init even for file-only use.
  target_link_libraries(asio INTERFACE ws2_32)
  message(STATUS "ASIO file I/O: Windows IOCP detected — asio::stream_file enabled automatically")
elseif(UNIX AND NOT APPLE)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBURING QUIET liburing)
  endif()
  if(LIBURING_FOUND)
    message(STATUS "ASIO file I/O: io_uring headers found — runtime lazy-loading enabled (no hard link to liburing)")
    # Provide liburing headers so file_asset.cpp can use io_uring types and inline helpers.
    # We do NOT link liburing here; the library is opened at runtime via dlopen so that
    # binaries can run on machines that do not have liburing installed (graceful sync fallback).
    # Note: ASIO_HAS_IO_URING is intentionally NOT set — we bypass ASIO's io_uring integration
    # and implement our own dlopen-based path in file_asset.cpp.
    target_include_directories(asio INTERFACE ${LIBURING_INCLUDE_DIRS})
    target_compile_definitions(asio INTERFACE EPIX_HAS_URING_HEADERS)
  else()
    message(STATUS "ASIO file I/O: io_uring not found — using synchronous fallback (install liburing-dev to enable)")
  endif()
else()
  message(STATUS "ASIO file I/O: platform not supported — using synchronous fallback")
endif()
