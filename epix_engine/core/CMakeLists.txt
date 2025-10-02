file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_core STATIC ${MODULE_SOURCES})
epix_parse_options(epix_core)
target_include_directories(epix_core PUBLIC ${MODULE_INCLUDES})

# --- tests ---
add_executable(test_untypedvec tests/test_untypedvec.cpp)
target_link_libraries(test_untypedvec PRIVATE epix_core)
add_test(NAME test_untypedvec COMMAND test_untypedvec)

# performance comparison test between untyped_vector and std::vector
add_executable(test_untypedvec_perf tests/test_untypedvec_perf.cpp)
target_link_libraries(test_untypedvec_perf PRIVATE epix_core)
add_test(NAME test_untypedvec_perf COMMAND test_untypedvec_perf)

# additional core unit tests
add_executable(test_bitvector tests/test_bitvector.cpp)
target_link_libraries(test_bitvector PRIVATE epix_core)
add_test(NAME test_bitvector COMMAND test_bitvector)

add_executable(test_type_registry tests/test_type_registry.cpp)
target_link_libraries(test_type_registry PRIVATE epix_core)
add_test(NAME test_type_registry COMMAND test_type_registry)

add_executable(test_sparse_set tests/test_sparse_set.cpp)
target_link_libraries(test_sparse_set PRIVATE epix_core)
add_test(NAME test_sparse_set COMMAND test_sparse_set)

add_executable(test_dense tests/test_dense.cpp)
target_link_libraries(test_dense PRIVATE epix_core)
add_test(NAME test_dense COMMAND test_dense)

add_executable(test_sparse_array tests/test_sparse_array.cpp)
target_link_libraries(test_sparse_array PRIVATE epix_core)
add_test(NAME test_sparse_array COMMAND test_sparse_array)

add_executable(test_table tests/test_table.cpp)
target_link_libraries(test_table PRIVATE epix_core)
add_test(NAME test_table COMMAND test_table)

add_executable(test_type_registry_mt tests/test_type_registry_mt.cpp)
target_link_libraries(test_type_registry_mt PRIVATE epix_core)
add_test(NAME test_type_registry_mt COMMAND test_type_registry_mt)