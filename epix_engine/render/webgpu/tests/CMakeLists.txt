cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
add_executable(wgpu_test ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(wgpu_test PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(wgpu_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(wgpu_test epix::app)
target_link_libraries(wgpu_test epix::window)
# target_link_libraries(wgpu_test epix::webgpu)
target_link_libraries(wgpu_test epix::input)
target_link_libraries(wgpu_test webgpu)