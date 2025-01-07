cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
add_executable(rdvk_test ${MODULE_SOURCES})
target_include_directories(rdvk_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(rdvk_test epix::app)
target_link_libraries(rdvk_test epix::window)
target_link_libraries(rdvk_test epix::render::vulkan2)
target_link_libraries(rdvk_test epix::input)