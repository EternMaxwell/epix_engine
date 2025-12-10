file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_image STATIC ${MODULE_SOURCES})

# Add C++20 module interface
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  target_sources(epix_image
    PUBLIC
      FILE_SET CXX_MODULES FILES
        src/epix_image.cppm
  )
endif()

epix_parse_options(epix_image)
target_include_directories(epix_image PUBLIC ${MODULE_INCLUDES})
target_compile_features(epix_image PUBLIC cxx_std_23)
target_link_libraries(epix_image PUBLIC epix_core)
target_link_libraries(epix_image PUBLIC epix_assets)
target_link_libraries(epix_image PUBLIC nvrhi)
target_link_libraries(epix_image PRIVATE stbimage)
