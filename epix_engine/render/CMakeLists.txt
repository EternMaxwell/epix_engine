file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_render STATIC ${MODULE_SOURCES})
epix_parse_options(epix_render)
target_include_directories(epix_render PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_render PUBLIC epix_core)
target_link_libraries(epix_render PUBLIC epix_window)