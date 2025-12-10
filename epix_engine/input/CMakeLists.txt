# ============================================================================
# epix_input Library Configuration
# ============================================================================

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_input STATIC ${MODULE_SOURCES})
epix_parse_options(epix_input)
target_include_directories(epix_input PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_input PUBLIC epix_core)

# C++20 Modules support (experimental)
if(EPIX_USE_MODULES)
  file(GLOB_RECURSE INPUT_MODULE_FILES CONFIGURE_DEPENDS "module/*.cppm")
  
  add_library(epix_input_modules STATIC)
  target_compile_definitions(epix_input_modules PUBLIC EPIX_USE_MODULES)
  target_link_libraries(epix_input_modules PUBLIC epix_core_modules)
  
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    target_sources(epix_input_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES ${INPUT_MODULE_FILES}
    )
  endif()
  
  target_link_libraries(epix_input_modules PRIVATE epix_input)
endif()