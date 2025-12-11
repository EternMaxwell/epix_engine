cmake_minimum_required(VERSION 3.21)
project(epix_module_test CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Testing module compilation with ${CMAKE_CXX_COMPILER_ID}")

# Simple test - just try to parse one module file
add_custom_target(check_modules
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++23 -fmodules-ts -fsyntax-only 
        ${CMAKE_SOURCE_DIR}/../epix_engine/core/modules/epix.core-api.cppm
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking module syntax"
)
