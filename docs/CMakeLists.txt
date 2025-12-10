cmake_minimum_required(VERSION 3.28)

# Enable C++20 modules before project()
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
endif()

project(ModuleTest CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create a library from the module interface
add_library(test_module)
target_sources(test_module
  PUBLIC
    FILE_SET CXX_MODULES FILES
      test_module.cppm
)
target_compile_features(test_module PUBLIC cxx_std_20)

# Create an executable that uses the module
add_executable(test_module_example test_module_main.cpp)
target_link_libraries(test_module_example PRIVATE test_module)
target_compile_features(test_module_example PRIVATE cxx_std_20)
