# Module implementation files (pure C++20 modules - no headers)
set(MODULE_SOURCES
    src/epix_transform_impl.cpp
)

list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_transform STATIC ${MODULE_SOURCES})

# Add C++20 module interface
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  target_sources(epix_transform
    PUBLIC
      FILE_SET CXX_MODULES FILES
        src/epix_transform.cppm
  )
endif()

epix_parse_options(epix_transform)
target_include_directories(epix_transform PUBLIC ${MODULE_INCLUDES})
target_compile_features(epix_transform PUBLIC cxx_std_23)
target_link_libraries(epix_transform PUBLIC epix_core)
target_link_libraries(epix_transform PUBLIC glm::glm)