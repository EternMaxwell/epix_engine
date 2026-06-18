epix_add_module(epix_ext_grid)
target_link_libraries(epix_ext_grid PUBLIC epix_core)
target_link_libraries(epix_ext_grid PUBLIC glm)

epix_add_tests(grid LINK_LIBS epix_ext_grid)

epix_add_examples(grid LINK_LIBS epix_ext_grid)