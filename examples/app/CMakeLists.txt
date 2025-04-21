cmake_minimum_required (VERSION 3.21)

add_executable(example_query query/main.cpp)
target_link_libraries(example_query PRIVATE epix_engine)

add_executable(example_resource resource/main.cpp)
target_link_libraries(example_resource PRIVATE epix_engine)