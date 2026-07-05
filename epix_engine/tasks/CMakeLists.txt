epix_add_module(epix_tasks)
target_link_libraries(epix_tasks PUBLIC epix_common)
target_link_libraries(epix_tasks PUBLIC epix_utils)
target_link_libraries(epix_tasks PUBLIC asio)

epix_add_tests(tasks LINK_LIBS epix_tasks)
epix_add_examples(tasks LINK_LIBS epix_tasks)