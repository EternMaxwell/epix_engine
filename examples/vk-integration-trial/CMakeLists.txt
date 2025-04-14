cmake_minimum_required (VERSION 3.21)

get_filename_component(PROJNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
set(PROJNAME "example_${PROJNAME}")
project(${PROJNAME})

#add self files
file(GLOB_RECURSE SOURCES "include/*.h" "include/*.hpp" "src/*.c" "src/*.cpp")
add_executable (${PROJNAME} ${SOURCES})
if(EPIX_ENABLE_TRACY)
    target_compile_definitions(${PROJNAME} PRIVATE EPIX_ENABLE_TRACY)
endif()
target_include_directories(${PROJNAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_include_directories(${PROJNAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/stb)

set_target_properties(${PROJNAME} PROPERTIES CXX_STANDARD 20)

target_link_libraries (${PROJNAME} PUBLIC earcut.hpp)

target_link_libraries (${PROJNAME} PRIVATE epix)
# target_compile_definitions(${PROJNAME} PRIVATE EPIX_SHARED)
# add_custom_command(TARGET ${PROJNAME} POST_BUILD
#   COMMAND ${CMAKE_COMMAND} -E copy 
#   $<TARGET_RUNTIME_DLLS:${PROJNAME}>
#   $<TARGET_FILE_DIR:${PROJNAME}>
#   COMMAND_EXPAND_LISTS
#   )