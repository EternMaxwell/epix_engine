epix_add_module(epix_task)
target_link_libraries(epix_task PUBLIC epix_common)
target_link_libraries(epix_task PUBLIC epix_utils)
target_link_libraries(epix_task PUBLIC asio)

epix_add_tests(task LINK_LIBS epix_task)
epix_add_examples(task LINK_LIBS epix_task)