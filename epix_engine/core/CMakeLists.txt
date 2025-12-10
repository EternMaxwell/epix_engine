# ============================================================================
# epix_core Library Configuration
# ============================================================================
# This library supports both traditional header-based compilation and
# C++20 module-based compilation.
#
# Options:
#   EPIX_USE_MODULES - Enable C++20 module support (experimental)
# ============================================================================

option(EPIX_USE_MODULES "Enable C++20 module support (experimental)" OFF)

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Check for C++20 modules support
if(EPIX_USE_MODULES)
  # Require CMake 3.28+ for proper modules support
  if(CMAKE_VERSION VERSION_LESS 3.28)
    message(WARNING "C++20 modules require CMake 3.28+. Falling back to header mode.")
    set(EPIX_USE_MODULES OFF)
  endif()

  # Check compiler support
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.29)
      message(WARNING "MSVC 19.29+ required for modules. Falling back to header mode.")
      set(EPIX_USE_MODULES OFF)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
      message(WARNING "Clang 16+ required for modules. Falling back to header mode.")
      set(EPIX_USE_MODULES OFF)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
      message(WARNING "GCC 14+ required for modules. Falling back to header mode.")
      set(EPIX_USE_MODULES OFF)
    endif()
  else()
    message(WARNING "Unknown compiler. Modules support untested. Falling back to header mode.")
    set(EPIX_USE_MODULES OFF)
  endif()
endif()

# Standard header-based library
add_library(epix_core STATIC ${MODULE_SOURCES})
epix_parse_options(epix_core)
target_include_directories(epix_core PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_core PUBLIC BSThreadPool)
target_link_libraries(epix_core PUBLIC spdlog::spdlog)
if (EPIX_ENABLE_TRACY)
  target_compile_definitions(epix_core PRIVATE EPIX_ENABLE_TRACY)
  target_link_libraries(epix_core PRIVATE TracyClient)
endif()

# C++20 Modules support (experimental)
if(EPIX_USE_MODULES)
  message(STATUS "Building epix_core with C++20 modules support (experimental)")
  
  # Collect module interface files
  file(GLOB_RECURSE MODULE_INTERFACE_FILES CONFIGURE_DEPENDS "module/*.cppm")
  file(GLOB_RECURSE MODULE_IMPL_FILES CONFIGURE_DEPENDS "module/*.cpp")
  
  # Create module library
  add_library(epix_core_modules STATIC)
  target_compile_definitions(epix_core_modules PUBLIC EPIX_USE_MODULES)
  target_link_libraries(epix_core_modules PUBLIC BSThreadPool)
  target_link_libraries(epix_core_modules PUBLIC spdlog::spdlog)
  
  # Add module sources using CMake's module support
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    target_sources(epix_core_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES ${MODULE_INTERFACE_FILES}
      PRIVATE
        ${MODULE_IMPL_FILES}
    )
  endif()
  
  # Link with standard epix_core for implementation
  target_link_libraries(epix_core_modules PRIVATE epix_core)
  
  if (EPIX_ENABLE_TRACY)
    target_compile_definitions(epix_core_modules PRIVATE EPIX_ENABLE_TRACY)
    target_link_libraries(epix_core_modules PRIVATE TracyClient)
  endif()
endif()

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS CORE_TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_core_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_core)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
