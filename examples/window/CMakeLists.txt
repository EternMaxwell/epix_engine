add_executable(
    epix_window_tests
    test.cpp
)

target_link_libraries(
    epix_window_tests
    PRIVATE epix
)