# ============================================================================
# epix_image Library Configuration
# ============================================================================

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_image STATIC ${MODULE_SOURCES})
epix_parse_options(epix_image)
target_include_directories(epix_image PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_image PUBLIC epix_core)
target_link_libraries(epix_image PUBLIC epix_assets)
target_link_libraries(epix_image PUBLIC nvrhi)
target_link_libraries(epix_image PRIVATE stbimage)

# C++20 Modules support (experimental)
if(EPIX_USE_MODULES)
  file(GLOB_RECURSE IMAGE_MODULE_FILES CONFIGURE_DEPENDS "module/*.cppm")
  
  add_library(epix_image_modules STATIC)
  target_compile_definitions(epix_image_modules PUBLIC EPIX_USE_MODULES)
  target_link_libraries(epix_image_modules PUBLIC epix_core_modules)
  target_link_libraries(epix_image_modules PUBLIC epix_assets_modules)
  target_link_libraries(epix_image_modules PUBLIC nvrhi)
  
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    target_sources(epix_image_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES ${IMAGE_MODULE_FILES}
    )
  endif()
  
  target_link_libraries(epix_image_modules PRIVATE epix_image)
endif()
