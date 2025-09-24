file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_core STATIC ${MODULE_SOURCES})
epix_parse_options(epix_core)
target_include_directories(epix_core PUBLIC ${MODULE_INCLUDES})