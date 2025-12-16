file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_transform STATIC ${MODULE_SOURCES})
epix_parse_options(epix_transform)
# Enable C++20 for module support
target_compile_features(epix_transform PUBLIC cxx_std_20)
target_include_directories(epix_transform PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_transform PUBLIC epix_core)
target_link_libraries(epix_transform PUBLIC glm::glm)