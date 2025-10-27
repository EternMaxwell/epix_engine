file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_assets STATIC ${MODULE_SOURCES})
epix_parse_options(epix_assets)
target_include_directories(epix_assets PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_assets PUBLIC epix_core)
target_link_libraries(epix_assets PUBLIC stduuid)