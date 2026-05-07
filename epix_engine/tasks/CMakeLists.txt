epix_add_module(epix_tasks)
target_include_directories(epix_tasks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(epix_tasks PUBLIC BSThreadPool)
target_link_libraries(epix_tasks PUBLIC asio)

epix_add_tests(tasks LINK_LIBS epix_tasks)
