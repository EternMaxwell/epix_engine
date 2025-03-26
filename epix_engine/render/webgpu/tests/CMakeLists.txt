cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
add_executable(wgpu_test ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(wgpu_test PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(wgpu_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(wgpu_test epix::app)
target_link_libraries(wgpu_test epix::window)
target_link_libraries(wgpu_test epix::webgpu)
target_link_libraries(wgpu_test epix::input)


if (GLFW_BUILD_COCOA)
    target_compile_definitions(wgpu_test PRIVATE _GLFW_COCOA)
    target_compile_options(wgpu_test PRIVATE -x objective-c)
    target_link_libraries(wgpu_test PRIVATE "-framework Cocoa" "-framework CoreVideo" "-framework IOKit" "-framework QuartzCore")
endif()
if (GLFW_BUILD_WIN32)
    target_compile_definitions(wgpu_test PRIVATE _GLFW_WIN32)
endif()
if (GLFW_BUILD_X11)
    target_compile_definitions(wgpu_test PRIVATE _GLFW_X11)
endif()
if (GLFW_BUILD_WAYLAND)
    target_compile_definitions(wgpu_test PRIVATE _GLFW_WAYLAND)
endif()

# set (PROJNAME wgpu_test)

# add_custom_command(TARGET ${PROJNAME} POST_BUILD
#   COMMAND ${CMAKE_COMMAND} -E copy 
#   $<TARGET_RUNTIME_DLLS:${PROJNAME}>
#   $<TARGET_FILE_DIR:${PROJNAME}>
#   COMMAND_EXPAND_LISTS
#   )