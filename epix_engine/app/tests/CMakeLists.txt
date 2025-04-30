cmake_minimum_required(VERSION 3.21)

project(pixel_engine_app_v4_tests)

file(GLOB_RECURSE TEST_SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE TEST_HEADERS "src/*.h" "src/*.hpp")

add_executable(pixel_engine_app_v4_tests
    ${TEST_SOURCES}
    ${TEST_HEADERS}
)

target_link_libraries(pixel_engine_app_v4_tests epix_app)
target_include_directories(pixel_engine_app_v4_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)