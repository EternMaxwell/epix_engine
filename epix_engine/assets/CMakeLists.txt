epix_add_module(epix_assets)
target_link_libraries(epix_assets PUBLIC epix_core)
target_link_libraries(epix_assets PUBLIC epix_tasks)
target_link_libraries(epix_assets PUBLIC stduuid)
target_link_libraries(epix_assets PUBLIC asio)
target_link_libraries(epix_assets PUBLIC efsw)
target_link_libraries(epix_assets PUBLIC zpp_bits)

epix_add_tests(assets LINK_LIBS epix_assets)