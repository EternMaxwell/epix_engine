cmake_minimum_required(VERSION 3.21)

set(EPIX_ENABLE_TRACY OFF)
set(EPIX_WITH_SHARED OFF)
option(EPIX_USE_VOLK "Use Vulkan Loader" ON)
set(EPIX_USE_VOLK ON)

function(epix_parse_options)
  if(EPIX_ENABLE_TRACY)
    target_compile_definitions(${ARGN} PRIVATE EPIX_ENABLE_TRACY)
  endif()
  if(EPIX_USE_VOLK)
    target_compile_definitions(${ARGN} PUBLIC EPIX_USE_VOLK)
  endif()
  if(EPIX_WITH_SHARED)
    target_compile_definitions(${ARGN} PUBLIC EPIX_SHARED)
    target_compile_definitions(${ARGN} PRIVATE EPIX_BUILD_SHARED)
  endif()
endfunction()

# Helper function to add module interface units to a target
# Usage: epix_add_module_interface(target_name module_name module_file [PARTITIONS partition1 partition2 ...])
function(epix_add_module_interface TARGET_NAME MODULE_NAME MODULE_FILE)
  if(NOT EPIX_ENABLE_MODULES)
    # Module support disabled, skip
    return()
  endif()
  
  cmake_parse_arguments(ARG "" "" "PARTITIONS" ${ARGN})
  
  # Add the module interface unit
  target_sources(${TARGET_NAME} PRIVATE ${MODULE_FILE})
  
  # Set compiler-specific properties for module interface file
  if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # MSVC requires /interface flag for module interface units
    set_source_files_properties(${MODULE_FILE} PROPERTIES
      LANGUAGE CXX
      COMPILE_FLAGS "/interface /std:c++20"
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang requires -fmodules flag
    set_source_files_properties(${MODULE_FILE} PROPERTIES
      LANGUAGE CXX
      COMPILE_FLAGS "-fmodules -std=c++20"
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC requires -fmodules-ts flag
    set_source_files_properties(${MODULE_FILE} PROPERTIES
      LANGUAGE CXX
      COMPILE_FLAGS "-fmodules-ts -std=c++20"
    )
  else()
    set_source_files_properties(${MODULE_FILE} PROPERTIES LANGUAGE CXX)
  endif()
  
  # Add partition interfaces if provided
  if(ARG_PARTITIONS)
    foreach(PARTITION ${ARG_PARTITIONS})
      target_sources(${TARGET_NAME} PRIVATE ${PARTITION})
      # Apply same compiler flags to partitions
      if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        set_source_files_properties(${PARTITION} PROPERTIES COMPILE_FLAGS "/interface /std:c++20")
      elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${PARTITION} PROPERTIES COMPILE_FLAGS "-fmodules -std=c++20")
      elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set_source_files_properties(${PARTITION} PROPERTIES COMPILE_FLAGS "-fmodules-ts -std=c++20")
      endif()
    endforeach()
  endif()
  
  # Ensure target has module support enabled definition
  # (Note: This is also called by epix_configure_module_target, but idempotent)
  target_compile_definitions(${TARGET_NAME} PUBLIC EPIX_MODULES_ENABLED)
endfunction()

# Helper function to configure a target for module or header build
# Usage: epix_configure_module_target(target_name)
function(epix_configure_module_target TARGET_NAME)
  if(EPIX_ENABLE_MODULES)
    # When modules are enabled, set compile definition to indicate this
    target_compile_definitions(${TARGET_NAME} PUBLIC EPIX_MODULES_ENABLED)
  endif()
endfunction()

add_subdirectory(core)
add_subdirectory(input)
add_subdirectory(assets)
add_subdirectory(window)
add_subdirectory(transform)
add_subdirectory(image)
add_subdirectory(render)
add_subdirectory(sprite)
add_subdirectory(text)