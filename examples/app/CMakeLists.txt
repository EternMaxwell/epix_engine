cmake_minimum_required (VERSION 3.21)

add_executable(example_query query/main.cpp)
target_link_libraries(example_query PRIVATE epix_engine)

add_executable(example_resource resource/main.cpp)
target_link_libraries(example_resource PRIVATE epix_engine)

add_executable(example_custom_system_param custom_system_param/main.cpp)
target_link_libraries(example_custom_system_param PRIVATE epix_engine)

add_executable(example_schedule schedule/main.cpp)
target_link_libraries(example_schedule PRIVATE epix_engine)
