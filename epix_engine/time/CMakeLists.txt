epix_add_module(epix_time)
target_link_libraries(epix_time PUBLIC epix_core)

epix_add_tests(time LINK_LIBS epix_time)
