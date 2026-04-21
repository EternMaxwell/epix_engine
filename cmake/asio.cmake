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