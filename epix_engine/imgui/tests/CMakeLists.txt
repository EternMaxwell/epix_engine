cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
add_executable(rd_imgui_test ${MODULE_SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(rd_imgui_test PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(rd_imgui_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(rd_imgui_test epix::app)
target_link_libraries(rd_imgui_test epix::window)
target_link_libraries(rd_imgui_test epix::render::vulkan2)
target_link_libraries(rd_imgui_test epix::input)
target_link_libraries(rd_imgui_test epix::imgui)