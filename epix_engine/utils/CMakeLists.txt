epix_add_module(epix_utils)
target_link_libraries(epix_utils PUBLIC epix_common)
target_link_libraries(epix_utils PUBLIC asio)

epix_add_tests(utils LINK_LIBS epix_utils)