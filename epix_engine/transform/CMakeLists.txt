# ============================================================================
# epix_transform Library Configuration
# ============================================================================

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_transform STATIC ${MODULE_SOURCES})
epix_parse_options(epix_transform)
target_include_directories(epix_transform PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_transform PUBLIC epix_core)
target_link_libraries(epix_transform PUBLIC glm::glm)

# C++20 Modules support (experimental)
if(EPIX_USE_MODULES)
  file(GLOB_RECURSE TRANSFORM_MODULE_FILES CONFIGURE_DEPENDS "module/*.cppm")
  
  add_library(epix_transform_modules STATIC)
  target_compile_definitions(epix_transform_modules PUBLIC EPIX_USE_MODULES)
  target_link_libraries(epix_transform_modules PUBLIC epix_core_modules)
  target_link_libraries(epix_transform_modules PUBLIC glm::glm)
  
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    target_sources(epix_transform_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES ${TRANSFORM_MODULE_FILES}
    )
  endif()
  
  target_link_libraries(epix_transform_modules PRIVATE epix_transform)
endif()