file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_input STATIC ${MODULE_SOURCES})
epix_parse_options(epix_input)
target_include_directories(epix_input PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_input PUBLIC epix_core)