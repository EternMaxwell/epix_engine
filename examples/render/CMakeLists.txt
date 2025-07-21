add_executable(
    epix_render_graph
    graph.cpp
)

target_link_libraries(
    epix_render_graph
    PRIVATE epix
)

add_executable(
    epix_render_nvrhi_test
    nvrhitest.cpp
)

target_link_libraries(
    epix_render_nvrhi_test
    PRIVATE epix
)