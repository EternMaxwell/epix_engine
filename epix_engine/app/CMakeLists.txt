cmake_minimum_required(VERSION 3.21)

file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
set(ENGINE_SOURCES ${ENGINE_SOURCES} ${MODULE_SOURCES} PARENT_SCOPE)
set(ENGINE_INCLUDES ${ENGINE_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
add_library(epix_app STATIC ${MODULE_SOURCES})
target_include_directories(epix_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(epix_app PUBLIC epix_common)
target_link_libraries(epix_app PUBLIC EnTT::EnTT)
target_link_libraries(epix_app PUBLIC BSThreadPool)
target_link_libraries(epix_app PUBLIC spdlog::spdlog)
target_link_libraries(epix_app PUBLIC sparsepp)
target_link_libraries(epix_app PUBLIC TracyClient)

add_library(epix::app ALIAS epix_app)

add_subdirectory(tests)
