cmake_minimum_required(VERSION 3.21)

project(epix_assets_tests)

file(GLOB_RECURSE TEST_SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE TEST_HEADERS "src/*.h" "src/*.hpp")

add_executable(epix_assets_tests
    ${TEST_SOURCES}
    ${TEST_HEADERS}
)

target_link_libraries(epix_assets_tests epix::app)
target_link_libraries(epix_assets_tests epix::assets)
target_include_directories(epix_assets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)