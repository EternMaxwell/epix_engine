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
endfunction()

function(epix_add_module MODULE_NAME)
    add_library(${MODULE_NAME} STATIC)
    epix_parse_options(${MODULE_NAME})
    target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS "src/*.ixx" "src/*.cppm")
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
    target_sources(${MODULE_NAME} 
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULES}
    )
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
add_subdirectory(render)
add_subdirectory(sprite)
add_subdirectory(text)