cmake_minimum_required(VERSION 3.21)

project(stbimage LANGUAGES CXX)

file(GLOB MODULE_SOURCES
    "src/stb_vorbis.c"
)

add_library(stbimage STATIC ${MODULE_SOURCES})
target_include_directories(stbimage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
