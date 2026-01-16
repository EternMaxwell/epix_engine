cmake_minimum_required(VERSION 3.21)

option(EPIX_ENABLE_TRACY "Enable tracy profiling" OFF)
option(EPIX_USE_VOLK "Use Vulkan Loader" ON)
option(EPIX_ENABLE_TEST "Enable epix test" ON)
option(EPIX_CXX_MODULE "Enable C++20 modules" ON)
option(EPIX_IMPORT_STD "Enable C++ import std" ON)

set(EPIX_CXX_MODULE ON)
# set(EPIX_ENABLE_TEST OFF)

# Print option statuses
message(STATUS "EPIX options:")
message(STATUS "  EPIX_ENABLE_TRACY (optional): ${EPIX_ENABLE_TRACY}")
message(STATUS "  EPIX_USE_VOLK     (optional): ${EPIX_USE_VOLK}")
message(STATUS "  EPIX_ENABLE_TEST  (optional): ${EPIX_ENABLE_TEST}")
message(STATUS "  EPIX_CXX_MODULE   (optional): ${EPIX_CXX_MODULE}")
message(STATUS "  EPIX_IMPORT_STD   (optional): ${EPIX_IMPORT_STD}")

function(epix_parse_options)
  if(EPIX_ENABLE_TRACY)
    target_compile_definitions(${ARGN} PRIVATE EPIX_ENABLE_TRACY)
  endif()
  if(EPIX_USE_VOLK)
    target_compile_definitions(${ARGN} PUBLIC EPIX_USE_VOLK)
  endif()
  if (EPIX_IMPORT_STD AND EPIX_CXX_MODULE)
    target_compile_definitions(${ARGN} PUBLIC EPIX_IMPORT_STD)
  endif()
  if(EPIX_ENABLE_TEST)
    target_compile_definitions(${ARGN} PRIVATE EPIX_ENABLE_TEST)
    target_link_libraries(${ARGN} PRIVATE GTest::gtest_main)
  endif()
endfunction()

function(epix_add_module MODULE_NAME)
  add_library(${MODULE_NAME} STATIC)
  epix_parse_options(${MODULE_NAME})
  target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS "modules/*.ixx" "modules/*.cppm")
  file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "modules/*.cpp")
  file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
  if (EPIX_CXX_MODULE)
    target_sources(${MODULE_NAME} 
      PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULES}
      PRIVATE ${MODULE_SOURCES}
    )
  endif()
  target_sources(${MODULE_NAME}
    PRIVATE ${SOURCES}
  )
endfunction()

function(epix_add_module_subdir MODULE_NAME SUBDIR)
  add_library(${MODULE_NAME} STATIC)
  epix_parse_options(${MODULE_NAME})
  target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS "modules/${SUBDIR}/*.ixx" "modules/${SUBDIR}/*.cppm")
  file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "modules/${SUBDIR}/*.cpp")
  file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/${SUBDIR}/*.c" "src/${SUBDIR}/*.cpp")
  if (EPIX_CXX_MODULE)
    target_sources(${MODULE_NAME} 
      PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULES}
      PRIVATE ${MODULE_SOURCES}
    )
  endif()
  target_sources(${MODULE_NAME}
    PRIVATE ${SOURCES}
  )
endfunction()

add_subdirectory(core)
add_subdirectory(input)
add_subdirectory(assets)
add_subdirectory(window)
add_subdirectory(transform)
add_subdirectory(image)
# add_subdirectory(render)
# add_subdirectory(sprite)
# add_subdirectory(text)