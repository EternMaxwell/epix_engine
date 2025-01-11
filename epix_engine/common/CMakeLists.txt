add_library(epix_common INTERFACE)
target_include_directories(epix_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)

add_library(epix::common ALIAS epix_common)