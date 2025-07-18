add_executable(
    epix_render_graph
    graph.cpp
)

target_link_libraries(
    epix_render_graph
    PRIVATE epix
)