# Module implementation files (pure C++20 modules - no headers)
set(MODULE_SOURCES
    src/epix_input_impl.cpp
)

list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_input STATIC ${MODULE_SOURCES})

# Add C++20 module interface
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  target_sources(epix_input
    PUBLIC
      FILE_SET CXX_MODULES FILES
        src/epix_input.cppm
  )
endif()

epix_parse_options(epix_input)
target_include_directories(epix_input PUBLIC ${MODULE_INCLUDES})
target_compile_features(epix_input PUBLIC cxx_std_23)
target_link_libraries(epix_input PUBLIC epix_core)