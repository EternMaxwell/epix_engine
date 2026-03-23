include(FetchContent)

FetchContent_Declare(
asio
GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
GIT_TAG asio-1-38-0
)
FetchContent_MakeAvailable(asio)