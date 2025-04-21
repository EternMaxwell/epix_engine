cmake_minimum_required (VERSION 3.21)

add_executable(query query/main.cpp)
target_link_libraries(query PRIVATE epix_engine)